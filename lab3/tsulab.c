#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/timekeeping.h>

static struct proc_dir_entry *tsulab_entry;

static int tsulab_show(struct seq_file *seq, void *unused)
{
	seq_printf(seq, "uptime_seconds: %lld\n",
		   (long long)ktime_get_boottime_seconds());
	return 0;
}

static int tsulab_open(struct inode *inode, struct file *file)
{
	return single_open(file, tsulab_show, NULL);
}

static const struct proc_ops tsulab_proc_ops = {
	.proc_open = tsulab_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static int __init tsulab_init(void)
{
	tsulab_entry = proc_create("tsulab", 0444, NULL, &tsulab_proc_ops);
	if (!tsulab_entry) {
		pr_err("tsulab: failed to create /proc/tsulab\n");
		return -ENOMEM;
	}

	pr_info("Welcome to the Tomsk State University\n");
	return 0;
}

static void __exit tsulab_exit(void)
{
	proc_remove(tsulab_entry);
	pr_info("Tomsk State University forever!\n");
}

module_init(tsulab_init);
module_exit(tsulab_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Linux system uptime in /proc/tsulab");
