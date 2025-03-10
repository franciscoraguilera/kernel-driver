/*
 * xserve_fp.c - USB driver for Apple Xserve Front Panel
 *
 * This is a sample USB driver for the Apple Xserve Front Panel.
 * It demonstrates basic USB driver functionalities including probe,
 * disconnect, and simple read/write operations using bulk transfers.
 *
 * Compile with the kernel build system and insert the module into a kernel
 * with a device matching the defined Vendor/Product IDs.
 */

 #include <linux/kernel.h>
 #include <linux/module.h>
 #include <linux/usb.h>
 #include <linux/slab.h>
 #include <linux/mutex.h>
 #include <linux/fs.h>
 #include <linux/uaccess.h>
 
 #define VENDOR_ID       0x05AC   /* Apple Vendor ID */
 #define PRODUCT_ID      0x821B   /* Sample Product ID for Xserve Front Panel */
 #define XSERVE_FP_BUFSIZE 512
 #define XSERVE_FP_MINOR_BASE 192
 
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
     unsigned char *bulk_in_buffer;
     size_t bulk_in_size;
     __u8 bulk_in_endpointAddr;
     __u8 bulk_out_endpointAddr;
     struct mutex io_mutex;  /* synchronize I/O */
 };
 
 /* Forward declarations for file operations */
 static int xserve_fp_open(struct inode *inode, struct file *file);
 static int xserve_fp_release(struct inode *inode, struct file *file);
 static ssize_t xserve_fp_read(struct file *file, char __user *buffer,
                               size_t count, loff_t *ppos);
 static ssize_t xserve_fp_write(struct file *file, const char __user *user_buffer,
                                size_t count, loff_t *ppos);
 
 /* File operations for the character device interface */
 static const struct file_operations xserve_fp_fops = {
     .owner   = THIS_MODULE,
     .read    = xserve_fp_read,
     .write   = xserve_fp_write,
     .open    = xserve_fp_open,
     .release = xserve_fp_release,
 };
 
 /* USB class driver info to register a minor number and create a device node */
 static struct usb_class_driver xserve_fp_class = {
     .name = "xserve_fp%d",
     .fops = &xserve_fp_fops,
     .minor_base = XSERVE_FP_MINOR_BASE,
 };
 
 /* Probe function: Called when a device matching our USB ID table is plugged in */
 static int xserve_fp_probe(struct usb_interface *interface,
                            const struct usb_device_id *id)
 {
     struct usb_device *udev = interface_to_usbdev(interface);
     struct xserve_fp *dev;
     struct usb_host_interface *iface_desc;
     struct usb_endpoint_descriptor *endpoint;
     int i, retval = -ENOMEM;
 
     /* Allocate memory for our device state and initialize it */
     dev = kzalloc(sizeof(*dev), GFP_KERNEL);
     if (!dev) {
         dev_err(&interface->dev, "Out of memory\n");
         goto error;
     }
     dev->udev = usb_get_dev(udev);
     dev->interface = interface;
     mutex_init(&dev->io_mutex);
 
     iface_desc = interface->cur_altsetting;
     /* Loop through endpoints to find a bulk-in and bulk-out endpoint */
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
         }
         if (usb_endpoint_is_bulk_out(endpoint)) {
             dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
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
 
     dev_info(&interface->dev,
              "Apple Xserve Front Panel USB device now attached as /dev/xserve_fp%d\n",
              interface->minor);
     return 0;
 
 error:
     if (dev)
         kfree(dev->bulk_in_buffer);
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
 
     usb_put_dev(dev->udev);
     kfree(dev->bulk_in_buffer);
     kfree(dev);
 }
 
 /* File operation: open
  *
  * This function locates the device structure based on the minor number.
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
  * This function reads data from the device via a bulk IN transfer.
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
  * This function writes data to the device via a bulk OUT transfer.
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
 MODULE_AUTHOR("Fran Aguilera <franciscoaguilera@ieee.org>");
 MODULE_DESCRIPTION("USB driver for Apple Xserve Front Panel"); 