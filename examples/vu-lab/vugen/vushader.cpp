// The registry behind VU_PROGRAM (src/vushader.hpp).
//
// A function-local static, not a namespace-scope vector: the programs register
// themselves during static init, and a namespace-scope container might not be
// constructed yet when the first one runs.
#include "vushader.hpp"

namespace vu {

namespace {
std::vector<Program*>& registry() {
    static std::vector<Program*> v;
    return v;
}
std::vector<Kernel*>& kernelRegistry() {
    static std::vector<Kernel*> v;
    return v;
}
}  // namespace

int registerProgram(Program* p) {
    registry().push_back(p);
    return (int)registry().size();
}

const std::vector<Program*>& registeredPrograms() { return registry(); }

int registerKernel(Kernel* k) {
    kernelRegistry().push_back(k);
    return (int)kernelRegistry().size();
}

const std::vector<Kernel*>& registeredKernels() { return kernelRegistry(); }

}  // namespace vu
