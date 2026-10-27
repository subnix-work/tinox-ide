// Test helper: the bridge returns strings allocated with Boehm GC, so any thread that
// calls it must be known to the collector. Tinox's own tasks are; plain std::threads
// of a C++ test have to attach themselves (docs/abi.md, "Threads").
#ifndef TINOXKDE_TEST_GC_THREAD_H
#define TINOXKDE_TEST_GC_THREAD_H

#ifndef GC_THREADS
#define GC_THREADS // exposes the thread registration API (GC_register_my_thread, ...)
#endif
#include <gc.h>

inline void gcInit()
{
    GC_INIT();
    GC_allow_register_threads();
}

/// Attaches the calling thread to the collector for the lifetime of the object.
class GcAttach
{
public:
    GcAttach()
    {
        GC_stack_base base;
        if (GC_get_stack_base(&base) == GC_SUCCESS) {
            m_attached = GC_register_my_thread(&base) == GC_SUCCESS;
        }
    }
    ~GcAttach()
    {
        if (m_attached) {
            GC_unregister_my_thread();
        }
    }
    GcAttach(const GcAttach &) = delete;
    GcAttach &operator=(const GcAttach &) = delete;

private:
    bool m_attached = false;
};

#endif // TINOXKDE_TEST_GC_THREAD_H
