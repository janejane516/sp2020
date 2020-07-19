#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#define MAX_LEN 512

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jane Shin");

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;
char *result_buffer;
ssize_t result_len;

static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
	pid_t input_pid;
	char *tmp_buffer;
	ssize_t tmp_len = 0;
	ssize_t N = MAX_LEN; // N = buffer size

	//Initialize result_len and allocate memory to buffers
	result_len = 0;
	result_buffer = (char *)kmalloc(N*sizeof(char), GFP_KERNEL);
	tmp_buffer = (char *)kmalloc(N*sizeof(char), GFP_KERNEL);
	//Initialize buffers
	memset(tmp_buffer, 0, sizeof(char)*N);
	memset(result_buffer, 0, sizeof(char)*N);

	if(copy_from_user(tmp_buffer, user_buffer, length))
		return -EFAULT;
    sscanf(tmp_buffer, "%u", &input_pid);
	// Find task_struct using input_pid.
    curr = pid_task(find_get_pid(input_pid), PIDTYPE_PID);
	if (!curr) {
		return -EINVAL;
	}
    // Tracing process tree from input_pid to init(1) process
	while(1) {
		// Initialize the tmp_buffer to place the process info.
		memset(tmp_buffer, 0, sizeof(char)*N);
		// Make Output Format string: process_command (process_id)
		tmp_len = sprintf(tmp_buffer, "%s (%d)\n", curr->comm, curr->pid);
		// if the buffers need more memory
		if((result_len + tmp_len) >= N) {
			N += MAX_LEN;
			tmp_buffer = (char *)krealloc(tmp_buffer, N*sizeof(char), GFP_KERNEL);
			result_buffer = (char *)krealloc(result_buffer, N*sizeof(char), GFP_KERNEL);
		}
		// append the tmp_buffer
		strncat(tmp_buffer, result_buffer, N-tmp_len-1);
		// update the result_buffer & result_len
		result_len += tmp_len;
		strncpy(result_buffer, tmp_buffer, N-1);
		// if the current task is init(1) process, done
		if(curr->pid == 1) {
			break;
		}
		// else, go to the parent process
		curr = curr->parent;
	}

	kfree(tmp_buffer);

    return length;
}

static ssize_t read_output(struct file *fp,
						char __user *user_buffer,
						size_t length,
						loff_t *position)
{
	// copy data from result_buffer to user_buffer
	ssize_t new_len;

	new_len = simple_read_from_buffer(user_buffer, length, position, result_buffer, result_len);

	return new_len;
}

static const struct file_operations dbfs_fops_write = {
    .write = write_pid_to_input,
};

static const struct file_operations dbfs_fops_read = {
	.read = read_output,
};


static int __init dbfs_module_init(void)
{ 
    // Creating the directory
    dir = debugfs_create_dir("ptree", NULL);
    if (!dir) {
		printk("Cannot create ptree dir\n");
		return -1;
    }

	// creating the input directory
    inputdir = debugfs_create_file("input", S_IRUSR|S_IWUSR, dir, NULL, &dbfs_fops_write);
	if (!inputdir) {
		printk("Cannot create inputdir file\n");
		return -1;
	}

	// creating the ptree directory
    ptreedir = debugfs_create_file("ptree", S_IRUSR|S_IWUSR, dir, NULL, &dbfs_fops_read);
	if (!ptreedir) {
		printk("Cannot create ptreedir file\n");
		return -1;
	}

	printk("dbfs_ptree module initialize done\n");

    return 0;
}

static void __exit dbfs_module_exit(void)
{
	// Implement exit module code
	kfree(result_buffer);
	debugfs_remove_recursive(dir);
	printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
