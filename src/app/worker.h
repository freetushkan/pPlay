//
// A thread for the ffmpeg work (media information, covers): decoders and TLS want a big
// stack, the SDL Switch port ignores the asked one.
//

#ifndef PPLAY_WORKER_H
#define PPLAY_WORKER_H

#ifdef __SWITCH__
#include <pthread.h>
#else
#include "cross2d/c2d.h"
#endif

namespace pplay {

    class Worker {

    public:

        Worker(int (*function)(void *), void *data);

        // joined
        ~Worker();

        void join();

        Worker(const Worker &) = delete;

        Worker &operator=(const Worker &) = delete;

    private:

#ifdef __SWITCH__
        static void *run(void *self);

        int (*function)(void *);
        void *data;
        pthread_t handle{};
        bool running = false;
#else
        c2d::C2DThread *thread = nullptr;
#endif
    };
}

#endif //PPLAY_WORKER_H
