#include "app/app.hpp"

int main(int argc, char **argv) {
  return app::run(app::Mode::Batch, argc, argv);
}
