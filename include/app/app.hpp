#ifndef APP_HPP
#define APP_HPP

namespace coding {

namespace app {

enum class Mode { Batch, Single, Interactive };

const char *binary_name(Mode mode) noexcept;

const char *description(Mode mode) noexcept;

int run(Mode mode, int argc, char **argv);

} // namespace app

} // namespace coding

#endif // APP_HPP
