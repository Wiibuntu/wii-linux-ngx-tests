/*
 * kexec.c - kexec_load system call
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2.  See the file COPYING for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/kexec.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>

#include "kexec_internal.h"

static int copy_user_segment_list(struct kimage *image,
				  unsigned long nr_segments,
				  struct kexec_segment __user *segments)
{
	int ret;
	size_t segment_bytes;

	/* Read in the segments */
	image->nr_segments = nr_segments;
	segment_bytes = nr_segments * sizeof(*segments);
	ret = copy_from_user(image->segment, segments, segment_bytes);
	if (ret)
		ret = -EFAULT;

	return ret;
}

static int kimage_alloc_init(struct kimage **rimage, unsigned long entry,
			     unsigned long nr_segments,
			     struct kexec_segment __user *segments,
			     unsigned long flags)
{
	int ret;
	struct kimage *image;
	bool kexec_on_panic = flags & KEXEC_ON_CRASH;

	if (kexec_on_panic) {
		/* Verify we have a valid entry point */
		if ((entry < phys_to_boot_phys(crashk_res.start)) ||
		    (entry > phys_to_boot_phys(crashk_res.end)))
			return -EADDRNOTAVAIL;
	}

	/* Allocate and initialize a controlling structure */
	image = do_kimage_alloc_init();
	if (!image)
		return -ENOMEM;

	image->start = entry;

	ret = copy_user_segment_list(image, nr_segments, segments);
	if (ret)
		goto out_free_image;

	if (kexec_on_panic) {
		/* Enable special crash kernel control page alloc policy. */
		image->control_page = crashk_res.start;
		image->type = KEXEC_TYPE_CRASH;
	}

	ret = sanity_check_segment_list(image);
	if (ret)
		goto out_free_image;

	/*
	 * Find a location for the control code buffer, and add it
	 * the vector of segments so that it's pages will also be
	 * counted as destination pages.
	 */
	ret = -ENOMEM;
	image->control_code_page = kimage_alloc_control_pages(image,
					   get_order(KEXEC_CONTROL_PAGE_SIZE));
	if (!image->control_code_page) {
		pr_err("Could not allocate control_code_buffer\n");
		goto out_free_image;
	}

	if (!kexec_on_panic) {
		image->swap_page = kimage_alloc_control_pages(image, 0);
		if (!image->swap_page) {
			pr_err("Could not allocate swap buffer\n");
			goto out_free_control_pages;
		}
	}

	*rimage = image;
	return 0;
out_free_control_pages:
	kimage_free_page_list(&image->control_pages);
out_free_image:
	kfree(image);
	return ret;
}

static int do_kexec_load(unsigned long entry, unsigned long nr_segments,
		struct kexec_segment __user *segments, unsigned long flags)
{
	struct kimage **dest_image, *image;
	unsigned long i;
	int ret;

	if (flags & KEXEC_ON_CRASH) {
		dest_image = &kexec_crash_image;
		if (kexec_crash_image)
			arch_kexec_unprotect_crashkres();
	} else {
		dest_image = &kexec_image;
	}

	if (nr_segments == 0) {
		/* Uninstall image */
		kimage_free(xchg(dest_image, NULL));
		return 0;
	}
	if (flags & KEXEC_ON_CRASH) {
		/*
		 * Loading another kernel to switch to if this one
		 * crashes.  Free any current crash dump kernel before
		 * we corrupt it.
		 */
		kimage_free(xchg(&kexec_crash_image, NULL));
	}

	ret = kimage_alloc_init(&image, entry, nr_segments, segments, flags);
	if (ret)
		return ret;

	if (flags & KEXEC_PRESERVE_CONTEXT)
		image->preserve_context = 1;

	ret = machine_kexec_prepare(image);
	if (ret)
		goto out;

	/*
	 * Some architecture(like S390) may touch the crash memory before
	 * machine_kexec_prepare(), we must copy vmcoreinfo data after it.
	 */
	ret = kimage_crash_copy_vmcoreinfo(image);
	if (ret)
		goto out;

	for (i = 0; i < nr_segments; i++) {
		ret = kimage_load_segment(image, &image->segment[i]);
		if (ret)
			goto out;
	}

	kimage_terminate(image);

	/* Install the new kernel and uninstall the old */
	image = xchg(dest_image, image);

out:
	if ((flags & KEXEC_ON_CRASH) && kexec_crash_image)
		arch_kexec_protect_crashkres();

	kimage_free(image);
	return ret;
}

/*
 * Exec Kernel system call: for obvious reasons only root may call it.
 *
 * This call breaks up into three pieces.
 * - A generic part which loads the new kernel from the current
 *   address space, and very carefully places the data in the
 *   allocated pages.
 *
 * - A generic part that interacts with the kernel and tells all of
 *   the devices to shut down.  Preventing on-going dmas, and placing
 *   the devices in a consistent state so a later kernel can
 *   reinitialize them.
 *
 * - A machine specific part that includes the syscall number
 *   and then copies the image to it's final destination.  And
 *   jumps into the image at entry.
 *
 * kexec does not sync, or unmount filesystems so if you need
 * that to happen you need to do that yourself.
 */

static DEFINE_MUTEX(kexec_mutex);

SYSCALL_DEFINE4(kexec_load, unsigned long, entry, unsigned long, nr_segments,
		struct kexec_segment __user *, segments, unsigned long, flags)
{
	int result;

	/* We only trust the superuser with rebooting the system. */
	if (!capable(CAP_SYS_BOOT) || kexec_load_disabled)
		return -EPERM;

	/*
	 * Verify we have a legal set of flags
	 * This leaves us room for future extensions.
	 */
	if ((flags & KEXEC_FLAGS) != (flags & ~KEXEC_ARCH_MASK))
		return -EINVAL;

	/* Verify we are on the appropriate architecture */
	if (((flags & KEXEC_ARCH_MASK) != KEXEC_ARCH) &&
		((flags & KEXEC_ARCH_MASK) != KEXEC_ARCH_DEFAULT))
		return -EINVAL;

	/* Put an artificial cap on the number
	 * of segments passed to kexec_load.
	 */
	if (nr_segments > KEXEC_SEGMENT_MAX)
		return -EINVAL;

	/* Because we write directly to the reserved memory
	 * region when loading crash kernels we need a mutex here to
	 * prevent multiple crash  kernels from attempting to load
	 * simultaneously, and to prevent a crash kernel from loading
	 * over the top of a in use crash kernel.
	 *
	 * KISS: always take the mutex.
	 */
	if (!mutex_trylock(&kexec_mutex))
		return -EBUSY;

	result = do_kexec_load(entry, nr_segments, segments, flags);

	mutex_unlock(&kexec_mutex);

	return result;
}

/*
 * Add and remove page tables for crashkernel memory
 *
 * Provide an empty default implementation here -- architecture
 * code may override this
 */
void __weak crash_map_reserved_pages(void)
{}

void __weak crash_unmap_reserved_pages(void)
{}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(kexec_load, compat_ulong_t, entry,
		       compat_ulong_t, nr_segments,
		       struct compat_kexec_segment __user *, segments,
		       compat_ulong_t, flags)
{
	struct compat_kexec_segment in;
	struct kexec_segment out, __user *ksegments;
	unsigned long i, result;

	/* Don't allow clients that don't understand the native
	 * architecture to do anything.
	 */
	if ((flags & KEXEC_ARCH_MASK) == KEXEC_ARCH_DEFAULT)
		return -EINVAL;

	if (nr_segments > KEXEC_SEGMENT_MAX)
		return -EINVAL;

	ksegments = compat_alloc_user_space(nr_segments * sizeof(out));
	for (i=0; i < nr_segments; i++) {
		result = copy_from_user(&in, &segments[i], sizeof(in));
		if (result)
			return -EFAULT;

		out.buf   = compat_ptr(in.buf);
		out.bufsz = in.bufsz;
		out.mem   = in.mem;
		out.memsz = in.memsz;

		result = copy_to_user(&ksegments[i], &out, sizeof(out));
		if (result)
			return -EFAULT;
	}

	return sys_kexec_load(entry, nr_segments, ksegments, flags);
}
#endif


void __weak crash_free_reserved_phys_range(unsigned long begin,
					   unsigned long end)
{
	unsigned long addr;

	for (addr = begin; addr < end; addr += PAGE_SIZE)
		free_reserved_page(pfn_to_page(addr >> PAGE_SHIFT));
}

static u32 *append_elf_note(u32 *buf, char *name, unsigned type, void *data,
			    size_t data_len)
{
	struct elf_note note;

	note.n_namesz = strlen(name) + 1;
	note.n_descsz = data_len;
	note.n_type   = type;
	memcpy(buf, &note, sizeof(note));
	buf += (sizeof(note) + 3)/4;
	memcpy(buf, name, note.n_namesz);
	buf += (note.n_namesz + 3)/4;
	memcpy(buf, data, note.n_descsz);
	buf += (note.n_descsz + 3)/4;

	return buf;
}

static void final_note(u32 *buf)
{
	struct elf_note note;

	note.n_namesz = 0;
	note.n_descsz = 0;
	note.n_type   = 0;
	memcpy(buf, &note, sizeof(note));
}

static int __init crash_notes_memory_init(void)
{
	/* Allocate memory for saving cpu registers. */
	crash_notes = alloc_percpu(note_buf_t);
	if (!crash_notes) {
		printk("Kexec: Memory allocation for saving cpu register"
		" states failed\n");
		return -ENOMEM;
	}
	return 0;
}
subsys_initcall(crash_notes_memory_init);


/*
 * parsing the "crashkernel" commandline
 *
 * this code is intended to be called from architecture specific code
 */


/*
 * This function parses command lines in the format
 *
 *   crashkernel=ramsize-range:size[,...][@offset]
 *
 * The function returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_mem(char 			*cmdline,
					unsigned long long	system_ram,
					unsigned long long	*crash_size,
					unsigned long long	*crash_base)
{
	char *cur = cmdline, *tmp;

	/* for each entry of the comma-separated list */
	do {
		unsigned long long start, end = ULLONG_MAX, size;

		/* get the start of the range */
		start = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warning("crashkernel: Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (*cur != '-') {
			pr_warning("crashkernel: '-' expected\n");
			return -EINVAL;
		}
		cur++;

		/* if no ':' is here, than we read the end */
		if (*cur != ':') {
			end = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warning("crashkernel: Memory "
						"value expected\n");
				return -EINVAL;
			}
			cur = tmp;
			if (end <= start) {
				pr_warning("crashkernel: end <= start\n");
				return -EINVAL;
			}
		}

		if (*cur != ':') {
			pr_warning("crashkernel: ':' expected\n");
			return -EINVAL;
		}
		cur++;

		size = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warning("Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (size >= system_ram) {
			pr_warning("crashkernel: invalid size\n");
			return -EINVAL;
		}

		/* match ? */
		if (system_ram >= start && system_ram < end) {
			*crash_size = size;
			break;
		}
	} while (*cur++ == ',');

	if (*crash_size > 0) {
		while (*cur && *cur != ' ' && *cur != '@')
			cur++;
		if (*cur == '@') {
			cur++;
			*crash_base = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warning("Memory value expected "
						"after '@'\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

/*
 * That function parses "simple" (old) crashkernel command lines like
 *
 * 	crashkernel=size[@offset]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_simple(char 		*cmdline,
					   unsigned long long 	*crash_size,
					   unsigned long long 	*crash_base)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warning("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	if (*cur == '@')
		*crash_base = memparse(cur+1, &cur);
	else if (*cur != ' ' && *cur != '\0') {
		pr_warning("crashkernel: unrecognized char\n");
		return -EINVAL;
	}

	return 0;
}

#define SUFFIX_HIGH 0
#define SUFFIX_LOW  1
#define SUFFIX_NULL 2
static __initdata char *suffix_tbl[] = {
	[SUFFIX_HIGH] = ",high",
	[SUFFIX_LOW]  = ",low",
	[SUFFIX_NULL] = NULL,
};

/*
 * That function parses "suffix"  crashkernel command lines like
 *
 *	crashkernel=size,[high|low]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_suffix(char *cmdline,
					   unsigned long long	*crash_size,
					   unsigned long long	*crash_base,
					   const char *suffix)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	/* check with suffix */
	if (strncmp(cur, suffix, strlen(suffix))) {
		pr_warn("crashkernel: unrecognized char\n");
		return -EINVAL;
	}
	cur += strlen(suffix);
	if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char\n");
		return -EINVAL;
	}

	return 0;
}

static __init char *get_last_crashkernel(char *cmdline,
			     const char *name,
			     const char *suffix)
{
	char *p = cmdline, *ck_cmdline = NULL;

	/* find crashkernel and use the last one if there are more */
	p = strstr(p, name);
	while (p) {
		char *end_p = strchr(p, ' ');
		char *q;

		if (!end_p)
			end_p = p + strlen(p);

		if (!suffix) {
			int i;

			/* skip the one with any known suffix */
			for (i = 0; suffix_tbl[i]; i++) {
				q = end_p - strlen(suffix_tbl[i]);
				if (!strncmp(q, suffix_tbl[i],
					     strlen(suffix_tbl[i])))
					goto next;
			}
			ck_cmdline = p;
		} else {
			q = end_p - strlen(suffix);
			if (!strncmp(q, suffix, strlen(suffix)))
				ck_cmdline = p;
		}
next:
		p = strstr(p+1, name);
	}

	if (!ck_cmdline)
		return NULL;

	return ck_cmdline;
}




static void update_vmcoreinfo_note(void)
{
	u32 *buf = vmcoreinfo_note;

	if (!vmcoreinfo_size)
		return;
	buf = append_elf_note(buf, VMCOREINFO_NOTE_NAME, 0, vmcoreinfo_data,
			      vmcoreinfo_size);
	final_note(buf);
}


/*
 * provide an empty default implementation here -- architecture
 * code may override this
 */
void __weak arch_crash_save_vmcoreinfo(void)
{}

unsigned long __weak paddr_vmcoreinfo_note(void)
{
	return __pa((unsigned long)(char *)&vmcoreinfo_note);
}

static int __init crash_save_vmcoreinfo_init(void)
{
	VMCOREINFO_OSRELEASE(init_uts_ns.name.release);
	VMCOREINFO_PAGESIZE(PAGE_SIZE);

	VMCOREINFO_SYMBOL(init_uts_ns);
	VMCOREINFO_SYMBOL(node_online_map);
#ifdef CONFIG_MMU
	VMCOREINFO_SYMBOL(swapper_pg_dir);
#endif
	VMCOREINFO_SYMBOL(_stext);
	VMCOREINFO_SYMBOL(vmap_area_list);

#ifndef CONFIG_NEED_MULTIPLE_NODES
	VMCOREINFO_SYMBOL(mem_map);
	VMCOREINFO_SYMBOL(contig_page_data);
#endif
#ifdef CONFIG_SPARSEMEM
	VMCOREINFO_SYMBOL(mem_section);
	VMCOREINFO_LENGTH(mem_section, NR_SECTION_ROOTS);
	VMCOREINFO_STRUCT_SIZE(mem_section);
	VMCOREINFO_OFFSET(mem_section, section_mem_map);
#endif
	VMCOREINFO_STRUCT_SIZE(page);
	VMCOREINFO_STRUCT_SIZE(pglist_data);
	VMCOREINFO_STRUCT_SIZE(zone);
	VMCOREINFO_STRUCT_SIZE(free_area);
	VMCOREINFO_STRUCT_SIZE(list_head);
	VMCOREINFO_SIZE(nodemask_t);
	VMCOREINFO_OFFSET(page, flags);
	VMCOREINFO_OFFSET(page, _count);
	VMCOREINFO_OFFSET(page, mapping);
	VMCOREINFO_OFFSET(page, lru);
	VMCOREINFO_OFFSET(page, _mapcount);
	VMCOREINFO_OFFSET(page, private);
	VMCOREINFO_OFFSET(pglist_data, node_zones);
	VMCOREINFO_OFFSET(pglist_data, nr_zones);
#ifdef CONFIG_FLAT_NODE_MEM_MAP
	VMCOREINFO_OFFSET(pglist_data, node_mem_map);
#endif
	VMCOREINFO_OFFSET(pglist_data, node_start_pfn);
	VMCOREINFO_OFFSET(pglist_data, node_spanned_pages);
	VMCOREINFO_OFFSET(pglist_data, node_id);
	VMCOREINFO_OFFSET(zone, free_area);
	VMCOREINFO_OFFSET(zone, vm_stat);
	VMCOREINFO_OFFSET(zone, spanned_pages);
	VMCOREINFO_OFFSET(free_area, free_list);
	VMCOREINFO_OFFSET(list_head, next);
	VMCOREINFO_OFFSET(list_head, prev);
	VMCOREINFO_OFFSET(vmap_area, va_start);
	VMCOREINFO_OFFSET(vmap_area, list);
	VMCOREINFO_LENGTH(zone.free_area, MAX_ORDER);
	log_buf_kexec_setup();
	VMCOREINFO_LENGTH(free_area.free_list, MIGRATE_TYPES);
	VMCOREINFO_NUMBER(NR_FREE_PAGES);
	VMCOREINFO_NUMBER(PG_lru);
	VMCOREINFO_NUMBER(PG_private);
	VMCOREINFO_NUMBER(PG_swapcache);
	VMCOREINFO_NUMBER(PG_slab);
#ifdef CONFIG_MEMORY_FAILURE
	VMCOREINFO_NUMBER(PG_hwpoison);
#endif
	VMCOREINFO_NUMBER(PAGE_BUDDY_MAPCOUNT_VALUE);

	arch_crash_save_vmcoreinfo();
	update_vmcoreinfo_note();

	return 0;
}

subsys_initcall(crash_save_vmcoreinfo_init);
