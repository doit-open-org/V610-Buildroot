
# CV610 user-space SDK environment
case ":${LD_LIBRARY_PATH:-}:" in
    *:/lib:*) ;;
    *) LD_LIBRARY_PATH="/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}" ;;
esac

SDK_RUNTIME_PRELOAD="/lib/libsecurec.so:/lib/libss_mpi_sysmem.so:/lib/libot_osal.so:/lib/libot_base.so:/lib/libot_sys.so:/lib/libot_irq.so:/lib/libot_vb.so:/lib/libot_vca.so:/lib/libot_pm.so:/lib/libot_chnl.so"
case ":${LD_PRELOAD:-}:" in
    *":${SDK_RUNTIME_PRELOAD}:"*) ;;
    *) LD_PRELOAD="${SDK_RUNTIME_PRELOAD}${LD_PRELOAD:+:${LD_PRELOAD}}" ;;
esac
unset SDK_RUNTIME_PRELOAD

export LD_LIBRARY_PATH LD_PRELOAD
