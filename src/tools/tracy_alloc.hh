#include <memory>
#include <vector>

/**
 * @brief   A small class demonstrating how to instrument for profiling.
 */
class alloc {
public:
    void operator()();

private:
    std::vector<std::shared_ptr<int>> m_xs;
};
