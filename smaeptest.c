#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/uaccess.h>        /* for copy_*_user */

MODULE_LICENSE("GPL");

static struct dentry *dir;

static ssize_t
smap_write(struct file *file, const char *buffer, size_t len, loff_t *off)
{
	/* simply read the address directly, without copy_from_user() */
	char junk[16];
	memcpy(junk, buffer, sizeof(junk));
	return len;
}

static ssize_t
smep_write(struct file *file, const char *buffer, size_t len, loff_t *off)
{
	/* assume the passed pointer contains a valid function */
	void (*ptr)(void) = (void*)buffer;
	ptr();
	return len;
}

static struct file_operations smap_ops = {
	.owner   = THIS_MODULE,
	.write   = smap_write,
};
static struct file_operations smep_ops = {
	.owner   = THIS_MODULE,
	.write   = smep_write,
};

int init_module(void)
{
	dir = debugfs_create_dir(THIS_MODULE->name, NULL);
	if (!dir)
		return -EINVAL;

	if (!debugfs_create_file("smap", 0222, dir, NULL, &smap_ops))
		return -EINVAL;

	if (!debugfs_create_file("smep", 0222, dir, NULL, &smep_ops))
		return -EINVAL;

	return 0;
}

void cleanup_module(void)
{
	debugfs_remove_recursive(dir);
}
