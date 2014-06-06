#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_AUTHOR("Jiri Jaburek <comps@nomail.dom>");
MODULE_LICENSE("GPL");

static struct module *tohold;

static char *modname = NULL;
module_param(modname, charp, 0);
MODULE_PARM_DESC(modname, "Module name to hold");

static int __init init_modholder(void)
{
	if (!modname) {
		printk(KERN_INFO "modholder: No module name specified.\n");
		return -EINVAL;
	}

	mutex_lock(&module_mutex);
	tohold = find_module(modname);
	mutex_unlock(&module_mutex);

	if (!tohold) {
		printk(KERN_INFO "modholder: Cannot find module %s.\n",
		       modname);
		return -EINVAL;
	}

	if (!try_module_get(tohold)) {
		printk(KERN_INFO "modholder: Cannot hold module %s.\n",
		       modname);
		return -EINVAL;
	}

	printk(KERN_INFO "modholder: Module %s held.\n", modname);

	return 0;
}

static void __exit cleanup_modholder(void)
{
	module_put(tohold);
	printk(KERN_INFO "modholder: Module %s released.\n", modname);
}

module_init(init_modholder);
module_exit(cleanup_modholder);
