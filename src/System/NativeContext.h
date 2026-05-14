// System/NativeContext.h
#ifndef SYSTEM_NATIVECONTEXT_H
#define SYSTEM_NATIVECONTEXT_H

#include <functional>

namespace System {
    class NativeContextGroup;

    class NativeContext {
    public:
        bool interrupted = false;
        NativeContext* groupNext = nullptr;
        NativeContext* groupPrev = nullptr;
        NativeContextGroup* group = nullptr;
        std::function<void()> procedure;

        NativeContext() = default;
        virtual ~NativeContext() = default;
    };
}

#endif // SYSTEM_NATIVECONTEXT_H
