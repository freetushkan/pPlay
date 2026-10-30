//
// Worker threads (see worker.h).
//

#include "worker.h"
#include "utility.h"

#define STACK_SIZE (4 * 1024 * 1024)

#ifdef __SWITCH__

pplay::Worker::Worker(int (*fn)(void *), void *arg) : function(fn), data(arg) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, STACK_SIZE);
    running = pthread_create(&handle, &attr, run, this) == 0;
    pthread_attr_destroy(&attr);
    if (!running) {
        // not much to do: the work done here, the caller waits for it anyway
        Utility::log(Utility::LogLevel::Error, "Worker: pthread_create failed");
        function(data);
    }
}

void *pplay::Worker::run(void *self) {
    auto *worker = (Worker *) self;
    worker->function(worker->data);
    return nullptr;
}

void pplay::Worker::join() {
    if (running) {
        pthread_join(handle, nullptr);
        running = false;
    }
}

pplay::Worker::~Worker() {
    join();
}

#else

pplay::Worker::Worker(int (*fn)(void *), void *arg) {
    thread = new c2d::C2DThread(fn, arg);
}

void pplay::Worker::join() {
    if (thread != nullptr) {
        thread->join();
        delete (thread);
        thread = nullptr;
    }
}

pplay::Worker::~Worker() {
    join();
}

#endif
