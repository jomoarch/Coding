#include "app/manager.hpp"

#include "app/list_view.hpp"
#include "app/viewer.hpp"
#include "base/color.hpp"
#include "base/text.hpp"
#include "store/store.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>
namespace coding {

namespace manager {

namespace {

struct Item {
  std::string id;
  std::vector<std::string> cells;
  bool pinned{false};
};

struct Model {
  Kind kind{Kind::Records};
  bool batch{false};
  std::size_t limit{0};
  std::uintmax_t trash_max{0};
  std::size_t trashed{0};

  std::vector<Item> items;
  std::string filter;
  std::string sort{"time"};
  std::size_t pinned_count{0};
};

std::string label(Kind kind) {
  switch (kind) {
  case Kind::Records:
    return "records";
  case Kind::Trash:
    return "trash";
  case Kind::Pins:
    return "protect list";
  }
  return "";
}

std::string program_name(Kind kind, bool batch) {
  const char *kind_name = kind == Kind::Records ? "rman"
                          : kind == Kind::Trash ? "tman"
                                                : "pin";
  return std::string(kind_name) + (batch ? "_b" : "_s");
}

bool contains_ci(const std::string &haystack, const std::string &needle) {
  if (needle.empty())
    return true;
  return text::to_lower(haystack).find(text::to_lower(needle)) !=
         std::string::npos;
}

bool is_pinned_listed(const store::PinListResult &pins, const std::string &id) {
  return std::any_of(
      pins.entries.begin(), pins.entries.end(),
      [&id](const store::PinEntry &pin) { return pin.id == id; });
}

bool build_model(Model &model, const std::filesystem::path &root,
                 std::string &error) {
  model.items.clear();

  if (model.kind == Kind::Pins) {
    const store::PinListResult pins = model.batch
                                          ? store::list_pins_batch(root)
                                          : store::list_pins_single(root);
    if (!pins) {
      error = pins.message;
      return false;
    }
    for (const store::PinEntry &pin : pins.entries) {
      if (!contains_ci(pin.name, model.filter) &&
          !contains_ci(pin.id, model.filter))
        continue;
      Item item;
      item.id = pin.id;
      item.pinned = true;
      item.cells = {pin.name, pin.id};
      model.items.push_back(std::move(item));
    }
    model.pinned_count = pins.entries.size();
    return true;
  }

  if (model.kind == Kind::Trash) {
    const store::TrashListResult trash = model.batch
                                             ? store::list_trash_batch(root)
                                             : store::list_trash_single(root);
    if (!trash) {
      error = trash.message;
      return false;
    }
    model.trashed = trash.entries.size();
    for (const store::TrashEntry &entry : trash.entries) {
      if (!contains_ci(entry.name, model.filter) &&
          !contains_ci(entry.id, model.filter))
        continue;
      Item item;
      item.id = entry.id;
      if (model.batch)
        item.cells = {std::to_string(entry.count),
                      std::to_string(entry.unmatched), entry.local_time,
                      entry.trashed_local, entry.id};
      else
        item.cells = {entry.name,
                      entry.status,
                      std::to_string(entry.unmatched),
                      entry.local_time,
                      entry.trashed_local,
                      entry.id};
      model.items.push_back(std::move(item));
    }
    return true;
  }

  if (model.batch) {
    const store::BatchListResult listed = store::list_batch(root);
    const store::PinListResult pins = store::list_pins_batch(root);
    if (!listed) {
      error = listed.message;
      return false;
    }
    if (!pins) {
      error = pins.message;
      return false;
    }
    for (const store::BatchEntry &entry : listed.entries) {
      if (!contains_ci(entry.id, model.filter))
        continue;
      Item item;
      item.id = entry.id;
      item.pinned = is_pinned_listed(pins, entry.id);
      item.cells = {std::to_string(entry.cases),
                    std::to_string(entry.matched),
                    std::to_string(entry.differ),
                    std::to_string(entry.unusable),
                    entry.local_time,
                    entry.id};
      model.items.push_back(std::move(item));
    }
  } else {
    const store::ListResult listed = store::list_single(root);
    const store::PinListResult pins = store::list_pins_single(root);
    if (!listed) {
      error = listed.message;
      return false;
    }
    if (!pins) {
      error = pins.message;
      return false;
    }
    for (const store::Entry &entry : listed.entries) {
      if (!contains_ci(entry.name, model.filter) &&
          !contains_ci(entry.id, model.filter))
        continue;
      Item item;
      item.id = entry.id;
      item.pinned = is_pinned_listed(pins, entry.id);
      item.cells = {entry.name, entry.status, std::to_string(entry.unmatched),
                    entry.local_time, entry.id};
      model.items.push_back(std::move(item));
    }
  }

  if (model.sort == "name") {
    std::sort(model.items.begin(), model.items.end(),
              [](const Item &a, const Item &b) {
                return a.cells.size() > 1 && b.cells.size() > 1 &&
                       a.cells[0] < b.cells[0];
              });
  } else if (model.sort == "unmatched") {
    const std::size_t column = model.batch ? 2 : 2;
    std::sort(model.items.begin(), model.items.end(),
              [column](const Item &a, const Item &b) {
                const long long x = a.cells.size() > column
                                        ? std::atoll(a.cells[column].c_str())
                                        : 0;
                const long long y = b.cells.size() > column
                                        ? std::atoll(b.cells[column].c_str())
                                        : 0;
                return x > y;
              });
  }

  return true;
}

std::vector<list_view::Column> columns_of(const Model &model) {
  if (model.kind == Kind::Pins)
    return {{"label", false}, {"id", false}};
  if (model.kind == Kind::Trash) {
    if (model.batch)
      return {{"cases", true},
              {"differ", true},
              {"time", false},
              {"trashed", false},
              {"id", false}};
    return {{"name", false}, {"state", false},   {"unmatched", true},
            {"time", false}, {"trashed", false}, {"id", false}};
  }
  if (model.batch)
    return {{"cases", true},    {"matched", true}, {"differ", true},
            {"unusable", true}, {"time", false},   {"id", false}};
  return {{"name", false},
          {"state", false},
          {"unmatched", true},
          {"time", false},
          {"id", false}};
}

std::string status_of(const Model &model) {
  std::string out = program_name(model.kind, model.batch) + "   " +
                    (model.batch ? "batch archive" : "single archive") + "   " +
                    label(model.kind) + ": " +
                    std::to_string(model.items.size());
  if (model.kind == Kind::Records) {
    out += "   protected " + std::to_string(model.pinned_count);
    out += model.limit == 0 ? "   limit none"
                            : "   limit " + std::to_string(model.limit);
  } else if (model.kind == Kind::Trash) {
    out += model.trash_max == 0
               ? "   cap none"
               : "   cap " + std::to_string(model.trash_max) + " bytes";
  }
  if (!model.filter.empty())
    out += "   find \"" + model.filter + "\"";
  if (model.sort != "time")
    out += "   sort " + model.sort;
  return out;
}

void refresh(Model &model, list_view::State &state,
             const std::filesystem::path &root) {
  std::string error;
  if (!build_model(model, root, error)) {
    state.message = error;
    return;
  }

  state.status = status_of(model);
  if (model.items.empty()) {
    model.items.clear();
    state.cursor = 0;
    state.top = 0;
    state.message =
        model.kind == Kind::Pins ? "the protect list is empty" : "no records";
    return;
  }
  if (state.cursor >= model.items.size())
    state.cursor = model.items.size() - 1;
}

std::string help_text(Kind kind) {
  std::string out =
      "keys: j/k or arrows move, ctrl+j/k scroll, q or Esc quit, : commands";
  switch (kind) {
  case Kind::Records:
    out +=
        "   Enter open   d delete (to trash)   p protect   Shift+p unprotect";
    out += "   commands: help, find <text>, sort time|name|unmatched, "
           "pin <id>, unpin <id>, del <id>";
    break;
  case Kind::Trash:
    out += "   r restore   d delete for good   Shift+d empty the trash";
    out += "   commands: help, find <text>, sort time|name|unmatched, "
           "restore <id>, del <id>";
    break;
  case Kind::Pins:
    out += "   d unprotect";
    out += "   commands: help, find <text>, sort time|name|unmatched, "
           "unpin <id>";
    break;
  }
  return out;
}

const Item *current(const Model &model, const list_view::State &state) {
  if (state.cursor >= model.items.size())
    return nullptr;
  return &model.items[state.cursor];
}

std::string unpin_or_pin(const Model &model, const std::filesystem::path &root,
                         const std::string &id, bool on) {
  const ResultBase done =
      model.batch
          ? (on ? store::pin_batch(root, id) : store::unpin_batch(root, id))
          : (on ? store::pin_single(root, id) : store::unpin_single(root, id));
  return done ? std::string() : done.message;
}

std::string delete_current(const Model &model,
                           const std::filesystem::path &root,
                           const std::string &id) {
  const store::TrashMoveResult moved = model.batch
                                           ? store::trash_batch(root, id)
                                           : store::trash_single(root, id);
  return moved ? std::string() : moved.message;
}

std::string restore_current(const Model &model,
                            const std::filesystem::path &root,
                            const std::string &id) {
  const ResultBase done = model.batch ? store::restore_batch(root, id)
                                      : store::restore_single(root, id);
  return done ? std::string() : done.message;
}

std::string erase_current(const Model &model, const std::filesystem::path &root,
                          const std::string &id) {
  const store::EraseResult done = model.batch
                                      ? store::erase_trash_batch(root, id)
                                      : store::erase_trash_single(root, id);
  return done ? std::string() : done.message;
}

void open_current(const Model &model, const std::filesystem::path &root,
                  const list_view::State &state) {
  const Item *item = current(model, state);
  if (item == nullptr)
    return;

  if (model.batch) {
    const store::BatchLoadResult loaded = store::load_batch(root, item->id);
    if (!loaded) {
      std::cerr << color::err("[view] ", loaded.message) << "\n";
      return;
    }
    viewer::view_batch(loaded.cases);
    return;
  }

  const store::LoadSingleResult loaded = store::load_single(root, item->id);
  if (!loaded) {
    std::cerr << color::err("[view] ", loaded.message) << "\n";
    return;
  }
  viewer::view(loaded.result);
}

bool run_command(Model &model, list_view::State &state,
                 const std::filesystem::path &root) {
  const std::string line(text::trim(state.input));
  state.input.clear();
  state.typing = false;
  if (line.empty())
    return true;

  const std::size_t space = line.find(' ');
  const std::string verb =
      text::to_lower(space == std::string::npos ? line : line.substr(0, space));
  const std::string rest =
      space == std::string::npos
          ? std::string()
          : std::string(text::trim(line.substr(space + 1)));

  if (verb == "help") {
    state.message = help_text(model.kind);
    return false;
  }
  if (verb == "find") {
    model.filter = rest;
    state.message =
        rest.empty() ? "showing everything" : "filtering by \"" + rest + "\"";
    state.cursor = 0;
    return true;
  }
  if (verb == "sort") {
    if (rest != "time" && rest != "name" && rest != "unmatched") {
      state.message = "sort needs time, name or unmatched";
      return false;
    }
    model.sort = rest;
    state.message = "sorted by " + rest;
    return true;
  }
  if (verb == "pin" || verb == "unpin") {
    if (model.kind == Kind::Trash) {
      state.message = "a record in the trash cannot be protected";
      return false;
    }
    if (rest.empty()) {
      state.message = verb + " needs an id";
      return false;
    }
    const std::string error = unpin_or_pin(model, root, rest, verb == "pin");
    state.message = error.empty() ? verb + "ped " + rest : error;
    return true;
  }
  if (verb == "del") {
    if (rest.empty()) {
      state.message = "del needs an id";
      return false;
    }
    const std::string error = model.kind == Kind::Trash
                                  ? erase_current(model, root, rest)
                                  : delete_current(model, root, rest);
    state.message = error.empty() ? "deleted " + rest : error;
    return true;
  }
  if (verb == "restore") {
    if (rest.empty()) {
      state.message = "restore needs an id";
      return false;
    }
    const std::string error = restore_current(model, root, rest);
    state.message = error.empty() ? "restored " + rest : error;
    return true;
  }

  state.message = "unknown command: " + verb + " (try help)";
  return false;
}

void step_after_delete(list_view::State &state, std::size_t before) {
  if (before == 0)
    return;
  if (state.cursor + 1 >= before && state.cursor > 0)
    --state.cursor;
}

} // namespace

void enforce_limits(const AppConfig &cfg, bool batch, std::string *note) {
  if (cfg.result_root.empty())
    return;

  const std::size_t limit = batch ? cfg.batch_max_count : cfg.single_max_count;
  const store::CountLimitResult counted =
      batch ? store::enforce_batch_limit(cfg.result_root, limit)
            : store::enforce_single_limit(cfg.result_root, limit);
  if (!counted && note != nullptr)
    *note = counted.message;

  const store::TrashLimitResult trimmed =
      batch ? store::limit_trash_batch(cfg.result_root, cfg.trash_max_bytes)
            : store::limit_trash_single(cfg.result_root, cfg.trash_max_bytes);
  if (!trimmed && note != nullptr)
    *note = trimmed.message;

  if (note != nullptr && counted.moved != 0)
    *note = std::to_string(counted.moved) + " record(s) moved to the trash";
}

int run(Kind kind, bool batch, const prompt::Options &cli,
        const AppConfig &cfg) {
  if (cfg.result_root.empty()) {
    std::cerr << color::err("[manage] [io].result_root is required: it is the "
                            "archive of comparisons")
              << "\n";
    return 2;
  }

  term::Session session;
  std::string error;
  if (!term::Session::open(session, error)) {
    std::cerr << color::err("[manage] ", error) << "\n";
    return 2;
  }

  Model model;
  model.kind = kind;
  model.batch = batch;
  model.limit = batch ? cfg.batch_max_count : cfg.single_max_count;
  model.trash_max = cfg.trash_max_bytes;

  enforce_limits(cfg, batch);

  term::Size size = session.size();
  list_view::State state;
  list_view::reshape(state, size.columns, size.rows, 0);
  refresh(model, state, cfg.result_root);

  while (true) {
    const std::vector<list_view::Column> columns = columns_of(model);
    std::vector<list_view::Row> rows;
    rows.reserve(model.items.size());
    for (const Item &item : model.items)
      rows.push_back(list_view::Row{item.cells, item.pinned});

    session.write(list_view::render(rows, state, size.rows, columns));

    list_view::Reading reading;
    if (!session.read_any(reading.key, reading.text))
      break;

    const std::size_t before = model.items.size();
    const list_view::Event event =
        list_view::apply(state, reading, size.rows, before);

    if (event == list_view::Event::Quit)
      break;
    if (event == list_view::Event::Moved || event == list_view::Event::None)
      continue;
    if (event == list_view::Event::Submit) {
      if (run_command(model, state, cfg.result_root))
        refresh(model, state, cfg.result_root);
      continue;
    }
    if (event == list_view::Event::Open) {
      open_current(model, cfg.result_root, state);
      refresh(model, state, cfg.result_root);
      continue;
    }

    const Item *item = current(model, state);
    const char key =
        reading.key == term::Key::Text ? static_cast<char>(reading.text) : '\0';
    std::string failure;

    if (kind == Kind::Records) {
      if (key == 'p' && item != nullptr)
        failure = unpin_or_pin(model, cfg.result_root, item->id, true);
      else if (key == 'P' && item != nullptr)
        failure = unpin_or_pin(model, cfg.result_root, item->id, false);
      else if (key == 'd' && item != nullptr)
        failure = delete_current(model, cfg.result_root, item->id);
      else
        continue;

      if (!failure.empty())
        state.message = failure;
      else
        step_after_delete(state, before);

      if (key == 'P') {
        std::string note;
        enforce_limits(cfg, batch, &note);
        if (!note.empty())
          state.message = note;
      }
      refresh(model, state, cfg.result_root);
      continue;
    }

    if (kind == Kind::Trash) {
      if (key == 'r' && item != nullptr)
        failure = restore_current(model, cfg.result_root, item->id);
      else if (key == 'd' && item != nullptr)
        failure = erase_current(model, cfg.result_root, item->id);
      else if (key == 'D') {
        const store::EraseResult emptied =
            batch ? store::empty_trash_batch(cfg.result_root)
                  : store::empty_trash_single(cfg.result_root);
        if (!emptied)
          failure = emptied.message;
        else
          state.message = "emptied " + std::to_string(emptied.removed) +
                          " record(s), " + std::to_string(emptied.freed_bytes) +
                          " bytes freed";
      } else {
        continue;
      }

      if (!failure.empty())
        state.message = failure;
      else
        step_after_delete(state, before);
      refresh(model, state, cfg.result_root);
      continue;
    }

    if (key == 'd' && item != nullptr)
      failure = unpin_or_pin(model, cfg.result_root, item->id, false);
    else
      continue;

    if (!failure.empty())
      state.message = failure;
    else
      step_after_delete(state, before);
    refresh(model, state, cfg.result_root);
  }

  session.close();
  prompt::pause_if_needed(cli.pause);
  return 0;
}

} // namespace manager
} // namespace coding
