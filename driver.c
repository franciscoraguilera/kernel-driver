/*
 * xserve_fp.c - USB driver for Apple Xserve Front Panel with device‑specific commands
 *               and interrupt endpoint handling.
 *
 * This driver demonstrates basic USB driver functionalities including probe,
 * disconnect, and read/write operations using bulk transfers, plus:
 *
 *  - A custom IOCTL interface for device‑specific commands.
 *      - XSERVE_FP_IOCTL_GET_STATUS: Retrieve device status.
 *      - XSERVE_FP_IOCTL_SET_LED: Set LED brightness (or similar JUST FOR EXAMPLE, ok?).
 *
 *  - Handling an interrupt endpoint to asynchronously receive events from the device.
 *
 */

 #include <linux/kernel.h>
 #include <linux/module.h>
 #include <linux/usb.h>
 #include <linux/slab.h>
 #include <linux/mutex.h>
 #include <linux/fs.h>
 #include <linux/uaccess.h>
 #include <linux/errno.h>
 
 #define VENDOR_ID         0x05AC   /* Apple Vendor ID */
 #define PRODUCT_ID        0x821B   /* Sample Product ID for Xserve Front Panel */
 #define XSERVE_FP_BUFSIZE 512
 #define XSERVE_FP_MINOR_BASE 192
 
 /* Device-specific IOCTL commands */
 #define XSERVE_FP_IOCTL_GET_STATUS _IOR('x', 1, int)
 #define XSERVE_FP_IOCTL_SET_LED    _IOW('x', 2, int)
 
 /* Table of devices that work with this driver */
 static const struct usb_device_id xserve_fp_table[] = {
     { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
     {} /* Terminating entry */
 };
 MODULE_DEVICE_TABLE(usb, xserve_fp_table);
 
 /* Device-specific structure */
 struct xserve_fp {
     struct usb_device *udev;
     struct usb_interface *interface;
 
     /* Bulk endpoints */
     unsigned char *bulk_in_buffer;
     size_t bulk_in_size;
     __u8 bulk_in_endpointAddr;
     __u8 bulk_out_endpointAddr;
 
     /* Interrupt endpoint for asynchronous events */
     unsigned char *irq_buffer;
     size_t irq_buffer_size;
     __u8 irq_endpointAddr;
     struct urb *irq_urb;
 
     struct mutex io_mutex;  /* synchronize I/O */
 };
 
 /* Forward declarations for file operations */
 static int xserve_fp_open(struct inode *inode, struct file *file);
 static int xserve_fp_release(struct inode *inode, struct file *file);
 static ssize_t xserve_fp_read(struct file *file, char __user *buffer,
                               size_t count, loff_t *ppos);
 static ssize_t xserve_fp_write(struct file *file, const char __user *user_buffer,
                                size_t count, loff_t *ppos);
 static long xserve_fp_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
 
 /* File operations for the character device interface */
 static const struct file_operations xserve_fp_fops = {
     .owner          = THIS_MODULE,
     .read           = xserve_fp_read,
     .write          = xserve_fp_write,
     .open           = xserve_fp_open,
     .release        = xserve_fp_release,
     .unlocked_ioctl = xserve_fp_ioctl,
 };
 
 /* USB class driver info to register a minor number and create a device node */
 static struct usb_class_driver xserve_fp_class = {
     .name = "xserve_fp%d",
     .fops = &xserve_fp_fops,
     .minor_base = XSERVE_FP_MINOR_BASE,
 };
 
 /* Interrupt URB callback function */
 static void xserve_fp_irq(struct urb *urb)
 {
     struct xserve_fp *dev = urb->context;
     int retval;
 
     if (urb->status) {
         dev_err(&dev->interface->dev,
                 "Interrupt URB error: %d\n", urb->status);
         return;
     }
 
     /* Process the interrupt data from dev->irq_buffer.
      * For example, you might parse event codes or update internal state.
      */
     dev_info(&dev->interface->dev,
              "Interrupt received: first byte = 0x%02x\n", dev->irq_buffer[0]);
 
     /* Resubmit the interrupt URB for continuous monitoring */
     retval = usb_submit_urb(urb, GFP_ATOMIC);
     if (retval)
         dev_err(&dev->interface->dev,
                 "Failed to resubmit interrupt URB: %d\n", retval);
 }
 
 /* Probe function: Called when a matching device is plugged in */
 static int xserve_fp_probe(struct usb_interface *interface,
                            const struct usb_device_id *id)
 {
     struct usb_device *udev = interface_to_usbdev(interface);
     struct xserve_fp *dev;
     struct usb_host_interface *iface_desc;
     struct usb_endpoint_descriptor *endpoint;
     int i, retval = -ENOMEM;
 
     /* Allocate and initialize our device structure */
     dev = kzalloc(sizeof(*dev), GFP_KERNEL);
     if (!dev) {
         dev_err(&interface->dev, "Out of memory\n");
         goto error;
     }
     dev->udev = usb_get_dev(udev);
     dev->interface = interface;
     mutex_init(&dev->io_mutex);
     dev->bulk_in_endpointAddr = 0;
     dev->bulk_out_endpointAddr = 0;
     dev->irq_endpointAddr = 0;
     dev->irq_buffer = NULL;
     dev->irq_urb = NULL;
 
     iface_desc = interface->cur_altsetting;
     /* Loop through endpoints and identify bulk and interrupt endpoints */
     for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
         endpoint = &iface_desc->endpoint[i].desc;
         if (usb_endpoint_is_bulk_in(endpoint)) {
             dev->bulk_in_size = usb_endpoint_maxp(endpoint);
             dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
             dev->bulk_in_buffer = kmalloc(dev->bulk_in_size, GFP_KERNEL);
             if (!dev->bulk_in_buffer) {
                 dev_err(&interface->dev, "Could not allocate bulk_in_buffer\n");
                 goto error;
             }
         } else if (usb_endpoint_is_bulk_out(endpoint)) {
             dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
         } else if (usb_endpoint_is_int_in(endpoint)) {
             dev->irq_buffer_size = usb_endpoint_maxp(endpoint);
             dev->irq_endpointAddr = endpoint->bEndpointAddress;
             dev->irq_buffer = kmalloc(dev->irq_buffer_size, GFP_KERNEL);
             if (!dev->irq_buffer) {
                 dev_err(&interface->dev, "Could not allocate irq_buffer\n");
                 goto error;
             }
         }
     }
 
     if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
         dev_err(&interface->dev,
                 "Could not find both bulk-in and bulk-out endpoints\n");
         retval = -ENODEV;
         goto error;
     }
 
     usb_set_intfdata(interface, dev);
 
     /* Register the device to get a minor number and create a /dev node */
     retval = usb_register_dev(interface, &xserve_fp_class);
     if (retval) {
         dev_err(&interface->dev,
                 "Not able to get a minor for this device.\n");
         usb_set_intfdata(interface, NULL);
         goto error;
     }
 
     /* Set up and submit the interrupt URB if an interrupt endpoint is available */
     if (dev->irq_buffer && dev->irq_endpointAddr) {
         dev->irq_urb = usb_alloc_urb(0, GFP_KERNEL);
         if (!dev->irq_urb) {
             dev_err(&interface->dev, "Could not allocate interrupt URB\n");
             retval = -ENOMEM;
             goto error;
         }
         usb_fill_int_urb(dev->irq_urb,
                          dev->udev,
                          usb_rcvintpipe(dev->udev, dev->irq_endpointAddr),
                          dev->irq_buffer,
                          dev->irq_buffer_size,
                          xserve_fp_irq,
                          dev,
                          iface_desc->desc.bInterval);
         retval = usb_submit_urb(dev->irq_urb, GFP_KERNEL);
         if (retval) {
             dev_err(&interface->dev, "Failed to submit interrupt URB: %d\n", retval);
             usb_free_urb(dev->irq_urb);
             dev->irq_urb = NULL;
             /* Depending on your needs, you might continue without interrupt support */
         }
     }
 
     dev_info(&interface->dev,
              "Apple Xserve Front Panel USB device now attached as /dev/xserve_fp%d\n",
              interface->minor);
     return 0;
 
 error:
     if (dev) {
         if (dev->irq_urb)
             usb_free_urb(dev->irq_urb);
         kfree(dev->bulk_in_buffer);
         kfree(dev->irq_buffer);
     }
     kfree(dev);
     return retval;
 }
 
 /* Disconnect function: Called when the device is unplugged */
 static void xserve_fp_disconnect(struct usb_interface *interface)
 {
     struct xserve_fp *dev = usb_get_intfdata(interface);
 
     usb_set_intfdata(interface, NULL);
     usb_deregister_dev(interface, &xserve_fp_class);
     dev_info(&interface->dev, "Apple Xserve Front Panel USB device now disconnected\n");
 
     if (dev->irq_urb) {
         usb_kill_urb(dev->irq_urb);
         usb_free_urb(dev->irq_urb);
     }
     usb_put_dev(dev->udev);
     kfree(dev->bulk_in_buffer);
     kfree(dev->irq_buffer);
     kfree(dev);
 }
 
 /* File operation: open
  *
  * Locate the device structure based on the minor number.
  */
 static int xserve_fp_open(struct inode *inode, struct file *file)
 {
     struct usb_interface *interface;
     struct xserve_fp *dev;
     int subminor = iminor(inode);
 
     interface = usb_find_interface(&xserve_fp_driver, subminor);
     if (!interface)
         return -ENODEV;
 
     dev = usb_get_intfdata(interface);
     if (!dev)
         return -ENODEV;
 
     file->private_data = dev;
     return 0;
 }
 
 /* File operation: release */
 static int xserve_fp_release(struct inode *inode, struct file *file)
 {
     return 0;
 }
 
 /* File operation: read
  *
  * Reads data from the device via a bulk IN transfer.
  */
 static ssize_t xserve_fp_read(struct file *file, char __user *buffer,
                               size_t count, loff_t *ppos)
 {
     struct xserve_fp *dev = file->private_data;
     int retval;
     int bytes_read;
 
     if (mutex_lock_interruptible(&dev->io_mutex))
         return -ERESTARTSYS;
 
     retval = usb_bulk_msg(dev->udev,
                           usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
                           dev->bulk_in_buffer,
                           min(dev->bulk_in_size, count),
                           &bytes_read,
                           5000);
     mutex_unlock(&dev->io_mutex);
 
     if (retval)
         return retval;
 
     if (copy_to_user(buffer, dev->bulk_in_buffer, bytes_read))
         return -EFAULT;
     return bytes_read;
 }
 
 /* File operation: write
  *
  * Writes data to the device via a bulk OUT transfer.
  */
 static ssize_t xserve_fp_write(struct file *file, const char __user *user_buffer,
                                size_t count, loff_t *ppos)
 {
     struct xserve_fp *dev = file->private_data;
     int retval;
     int bytes_written;
     char *buf;
 
     buf = kmalloc(count, GFP_KERNEL);
     if (!buf)
         return -ENOMEM;
 
     if (copy_from_user(buf, user_buffer, count)) {
         kfree(buf);
         return -EFAULT;
     }
 
     if (mutex_lock_interruptible(&dev->io_mutex)) {
         kfree(buf);
         return -ERESTARTSYS;
     }
     retval = usb_bulk_msg(dev->udev,
                           usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
                           buf,
                           count,
                           &bytes_written,
                           5000);
     mutex_unlock(&dev->io_mutex);
     kfree(buf);
     if (retval)
         return retval;
     return bytes_written;
 }
 
 /* File operation: ioctl
  *
  * Handle device‑specific commands via IOCTL.
  */
 static long xserve_fp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
 {
     struct xserve_fp *dev = file->private_data;
     int retval = 0;
     int status;
     int led_val;
 
     if (mutex_lock_interruptible(&dev->io_mutex))
         return -ERESTARTSYS;
 
     switch (cmd) {
     case XSERVE_FP_IOCTL_GET_STATUS:
         /* Example: Retrieve status via a vendor-specific control message.
          * bRequest value 0x01 is arbitrary and should be defined per your hardware.
          */
         retval = usb_control_msg(dev->udev,
                                  usb_rcvctrlpipe(dev->udev, 0),
                                  0x01, /* bRequest for GET_STATUS */
                                  USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                  0, 0,
                                  &status, sizeof(status),
                                  1000);
         if (retval < 0)
             goto out;
         if (copy_to_user((int __user *)arg, &status, sizeof(status))) {
             retval = -EFAULT;
             goto out;
         }
         retval = 0;
         break;
 
     case XSERVE_FP_IOCTL_SET_LED:
         /* Example: Set LED brightness (or similar) via a vendor-specific control message.
          * bRequest value 0x02 is arbitrary and should match your hardware specification.
          */
         if (copy_from_user(&led_val, (int __user *)arg, sizeof(led_val))) {
             retval = -EFAULT;
             goto out;
         }
         retval = usb_control_msg(dev->udev,
                                  usb_sndctrlpipe(dev->udev, 0),
                                  0x02, /* bRequest for SET_LED */
                                  USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
                                  led_val, 0,
                                  NULL, 0,
                                  1000);
         break;
 
     default:
         retval = -ENOTTY;
         break;
     }
 
 out:
     mutex_unlock(&dev->io_mutex);
     return retval;
 }
 
 /* USB driver structure */
 static struct usb_driver xserve_fp_driver = {
     .name       = "xserve_fp",
     .id_table   = xserve_fp_table,
     .probe      = xserve_fp_probe,
     .disconnect = xserve_fp_disconnect,
 };
 
 /* Module initialization */
 static int __init xserve_fp_init(void)
 {
     int result;
     result = usb_register(&xserve_fp_driver);
     if (result)
         pr_err("usb_register failed. Error number %d\n", result);
     return result;
 }
 
 /* Module exit */
 static void __exit xserve_fp_exit(void)
 {
     usb_deregister(&xserve_fp_driver);
 }
 
 module_init(xserve_fp_init);
 module_exit(xserve_fp_exit);
 
 MODULE_LICENSE("GPL");
 MODULE_AUTHOR("Fran <franciscoaguilera@ieee.org>");
 MODULE_DESCRIPTION("USB driver for Apple Xserve Front Panel with device-specific commands and interrupt endpoint handling"); 