#ifndef __EXCPT_LINUX_H__
#define __EXCPT_LINUX_H__

namespace avxsynth {

/////////////////////////////////
// Copied from Windows excpt.h
/////////////////////////////////

typedef enum _EXCEPTION_DISPOSITION {
    ExceptionContinueExecution,
    ExceptionContinueSearch,
    ExceptionNestedException,
    ExceptionCollidedUnwind
} EXCEPTION_DISPOSITION;



}; // namespace avxsynth

#endif //  __EXCPT_LINUX_H__
