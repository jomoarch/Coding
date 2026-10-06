#include "app/app.hpp"

using namespace coding;

int main(int argc, char **argv) {
  return app::run(app::Mode::Batch, argc, argv);
}
