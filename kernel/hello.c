#include <linux/linkage.h>
#include <linux/kernel.h>
#include <asm/uaccess.h>
#define MAX_BUF_SIZE 1000
asmlinkage int sys_hello(char __user *buff, int len) {
	char tmp[MAX_BUF_SIZE]; /* tmp buffer to copy user’s string into */
	printk(KERN_EMERG "Entering helloworld(). The len is %d\n", len);
	
	if ( (len < 1) || (len > MAX_BUF_SIZE) ) {
		printk(KERN_EMERG "helloworld() failed: illegal len (%d)!", len);
		return(-1);
	}
	/* copy buff from user space into a kernel buffer */
	if (copy_from_user(tmp, buff, len)) {
		printk(KERN_EMERG "helloworld() failed: copy_from_user() error");
		return(-1);
	}
	tmp[len] = '\0';
	printk(KERN_EMERG "Hello World from %s.\n", tmp);
	printk(KERN_EMERG "Exiting helloworld().\n");
	return(0);
}
