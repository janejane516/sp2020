#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jane Shin");

static struct dentry *dir, *output;
static struct task_struct *task;



unsigned long power(long base, unsigned int exp) {
	unsigned long res = 1;
	while(exp) {
		if(exp & 1) {
			res *= base;
		}
		exp >>= 1;
		base *= base;
	}
	return res;
}

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
	pid_t input_pid = 0;
	unsigned long vaddr = 0;
	int i;

	struct mm_struct *mm;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;
	phys_addr_t pfn;

	unsigned char my_buffer[length*2];
	unsigned char result_buffer[length*2];
	memset(my_buffer, 0, length*2);
	memset(result_buffer, 0, length*2);

	// Get pid of app
	if(copy_from_user(my_buffer, user_buffer, length))
		return -EFAULT;
	for(i=0; i<4; i++) {
		input_pid += (int)(my_buffer[i])*power(256, i);
	}
	task = pid_task(find_get_pid(input_pid), PIDTYPE_PID);
	if(!task) {
		return -EINVAL;
	}
	// Get the pointer of the top level page entry (pgd)
	mm = task->mm;
	// Compute the virtual address (48 bits)
	for(i=0; i<6; i++) {
		vaddr += (unsigned long)(my_buffer[i+8])*power(256, i);
	}

	// Page walk procedure in Linux 4.15.0: pgd->p4d->pud->pmd->pte
	pgdp = pgd_offset(mm, vaddr);
	if(pgd_none(*pgdp) || pgd_bad(*pgdp)) {
		return -EINVAL;
	}
	p4dp = p4d_offset(pgdp, vaddr);
	if(p4d_none(*p4dp) || p4d_bad(*p4dp)) {
		return -EINVAL;
	}
	pudp = pud_offset(p4dp, vaddr);
	if(pud_none(*pudp) || pud_bad(*pudp)) {
		return -EINVAL;
	}
	pmdp = pmd_offset(pudp, vaddr);
	if(pmd_none(*pmdp) || pmd_bad(*pmdp)) {
		return -EINVAL;
	}
	ptep = pte_offset_kernel(pmdp, vaddr);
	if(pte_none(*ptep) || !pte_present(*ptep)) {
		return -EINVAL;
	}
	// Get the pfn value
	pfn = pte_pfn(*ptep);

	// Return the physical address
	for(i=0; i<length; i++) {
		// Copy pid & virtual address & paddings (which are originally from user_buffer)
		if(i<16) {
			result_buffer[i] = my_buffer[i];
		}
		else if(i<17) {
			result_buffer[i] = my_buffer[i-8];
		}
		else if(i<18) {
			result_buffer[i] = my_buffer[i-8] % 16;
			result_buffer[i] += (pfn % 16) * 16;
			pfn /= 16;
		}
		else {
			result_buffer[i] = pfn % 16;
			pfn /= 16;
			result_buffer[i] += (pfn % 16) * 16;
			pfn /= 16;
		}
	}
	result_buffer[length] = '\0';
	copy_to_user(user_buffer, result_buffer, length);

	return length;
}

static const struct file_operations dbfs_fops = {
	// Mapping file operations with your functions
	.read = read_output,
};

static int __init dbfs_module_init(void)
{

    dir = debugfs_create_dir("paddr", NULL);

    if (!dir) {
		printk("Cannot create paddr dir\n");
		return -1;
    }

    output = debugfs_create_file("output", S_IRUSR|S_IWUSR, dir, NULL, &dbfs_fops);
	if (!output) {
		printk("Cannot create output file\n");
		return -1;
	}

	printk("dbfs_paddr module initialize done\n");

    return 0;
}

static void __exit dbfs_module_exit(void)
{
    debugfs_remove_recursive(dir);
	printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
