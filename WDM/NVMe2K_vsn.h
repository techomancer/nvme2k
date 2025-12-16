#define NVMe2K_VER_NUM           2,2,0,0
#define NVMe2K_VER_STR          "2.2.0.0"

#if     defined( _IA64_  )

#define NVMe2K_DRIVER           "NVMEe2Ki64.sys"

#elif   defined( _AMD64_ )

#define NVMe2K_DRIVER           "NVMEe2Kx64.sys"

#elif   defined( _X86_   )

#define NVMe2K_DRIVER           "NVMEe2Kx32.sys"

#else

#define NVMe2K_DRIVER           "NVMEe2KuNN.sys"

#endif
