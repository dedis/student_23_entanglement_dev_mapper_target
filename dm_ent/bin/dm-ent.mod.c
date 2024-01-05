#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif


static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xd43ef8ce, "dm_put_device" },
	{ 0x37a0cba, "kfree" },
	{ 0xca21ebd3, "bitmap_free" },
	{ 0xa897e3e7, "mempool_free" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0x122c3a7e, "_printk" },
	{ 0x1953c958, "mempool_create" },
	{ 0xa19b956, "__stack_chk_fail" },
	{ 0x20dbf27, "bitmap_alloc" },
	{ 0xc3762aec, "mempool_alloc" },
	{ 0xc841ba37, "submit_bio" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0x13be839e, "bioset_exit" },
	{ 0xd92519fc, "bio_put" },
	{ 0x96c1e28, "bio_endio" },
	{ 0xd985dc99, "mempool_free_pages" },
	{ 0x585cc20a, "dm_unregister_target" },
	{ 0x535b2efb, "bio_add_page" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x89940875, "mutex_lock_interruptible" },
	{ 0x3b386c21, "bio_alloc_clone" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0xa21668d4, "dm_register_target" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x2f38b402, "bioset_init" },
	{ 0x9034a696, "mempool_destroy" },
	{ 0xe5720083, "dm_table_get_mode" },
	{ 0x41ed3709, "get_random_bytes" },
	{ 0x3d650a07, "kmalloc_trace" },
	{ 0xcb5760ab, "dm_get_device" },
	{ 0x1b2e9fb8, "bio_alloc_bioset" },
	{ 0x70fddae9, "submit_bio_wait" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0xe2c17b5d, "__SCT__might_resched" },
	{ 0x7affd727, "kmalloc_caches" },
	{ 0x766a0927, "mempool_alloc_pages" },
	{ 0x453e7dc, "module_layout" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "326F6F0060C755609BF5AFC");
