#include "app/viewer.hpp"

#include "base/color.hpp"
#include "base/text.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

namespace viewer {

namespace {

constexpr std::size_t kDetailIndent = 4;
constexpr std::string_view kIndent = "    ";

std::size_t digits(std::size_t value) noexcept {
  std::size_t count = 1;
  while (value >= 10) {
    value /= 10;
    ++count;
  }
  return count;
}

std::size_t fit_width(std::size_t width) noexcept {
  return width == 0 ? 1 : width;
}

std::string clip(std::string_view text, std::size_t width) {
  std::string out;
  std::size_t columns = 0;
  for (std::size_t i = 0; i < text.size();) {
    const text::CharWidth w = text::measure(text, i);
    if (columns + w.columns > width)
      break;
    out.append(text.data() + i, w.bytes);
    columns += w.columns;
    i += w.bytes;
  }
  return out;
}

std::string ellipsize(std::string_view text, std::size_t width) {
  if (text::display_width(text) <= width)
    return std::string(text);
  if (width <= 3)
    return clip(text, width);
  return clip(text, width - 3) + "...";
}

std::vector<std::string> wrap(std::string_view text, std::size_t width) {
  std::vector<std::string> rows;
  const std::size_t limit = fit_width(width);
  std::size_t columns = 0;
  std::string row;
  for (std::size_t i = 0; i < text.size();) {
    const text::CharWidth w = text::measure(text, i);
    if (columns + w.columns > limit) {
      rows.push_back(std::move(row));
      row.clear();
      columns = 0;
    }
    row.append(text.data() + i, w.bytes);
    columns += w.columns;
    i += w.bytes;
  }
  rows.push_back(std::move(row));
  return rows;
}

std::size_t list_rows(std::size_t height, std::size_t header_rows) noexcept {
  return height > header_rows ? height - header_rows : 0;
}

std::size_t header_rows(const CompareResult &result) noexcept {
  return result.warning.empty() ? 1 : 2;
}

std::size_t detail_width(const State &state) noexcept {
  return fit_width(state.width > kDetailIndent ? state.width - kDetailIndent
                                               : 1);
}

std::string explanation(const LineDiff &diff) {
  switch (diff.kind) {
  case LineKind::OnlyExpect:
    return "only in expect: " + diff.expect_line;
  case LineKind::OnlyOutput:
    return "only in output: " + diff.output_line;
  case LineKind::Differ:
    break;
  }
  return "whitespace differs: " + diff.output_line;
}

std::size_t columns_for_index(std::size_t count) noexcept {
  return digits(count);
}

std::size_t columns_for_line(const State &state) noexcept {
  std::size_t longest = 0;
  for (const Node &node : state.nodes)
    longest = std::max(longest, node.diff->line_no);
  return digits(longest);
}

std::size_t rows_before(const State &state, std::size_t index) noexcept {
  std::size_t rows = 0;
  for (std::size_t i = 0; i < index && i < state.nodes.size(); ++i)
    rows += node_height(state.nodes[i]);
  return rows;
}

std::size_t total_rows(const State &state) noexcept {
  std::size_t rows = 0;
  for (const Node &node : state.nodes)
    rows += node_height(node);
  return rows;
}

void build_detail(State &state, Node &node) {
  node.detail.clear();
  if (node.token_case)
    return;
  for (std::string &row : wrap(explanation(*node.diff), detail_width(state)))
    node.detail.push_back(std::move(row));
}

void expand(State &state, Node &node) {
  node.expanded = true;
  node.token = 0;
  build_detail(state, node);
}

void collapse(Node &node) {
  node.expanded = false;
  node.detail.clear();
}

void clamp_range(State &state, std::size_t height) {
  const std::size_t visible = list_rows(height, state.header);
  const std::size_t total = total_rows(state);
  state.top = std::min(state.top, total > visible ? total - visible : 0);
}

void clamp_to_cursor(State &state, std::size_t height) {
  const std::size_t visible = list_rows(height, state.header);
  if (!state.nodes.empty()) {
    const std::size_t row = rows_before(state, state.cursor);
    const std::size_t node = node_height(state.nodes[state.cursor]);
    if (row < state.top) {
      state.top = row;
    } else if (node <= visible && row + node > state.top + visible) {
      state.top = row + node - visible;
    }
  }
  clamp_range(state, height);
}

std::string node_row(const State &state, const Node &node, std::size_t index,
                     std::size_t sub) {
  if (sub == 0) {
    const std::size_t number = index + 1;
    std::string row = text::pad_left(std::to_string(number),
                                     columns_for_index(state.nodes.size()));
    row += ' ';
    row += text::pad_left(std::to_string(node.diff->line_no),
                          columns_for_line(state));
    row += ' ';
    row += node.marker;
    return row;
  }

  if (node.token_case) {
    const std::size_t total = node.diff->token_diffs.size();
    const std::size_t current = std::min(node.token, total - 1);
    const TokenDiff &token = node.diff->token_diffs[current];

    if (sub == 1) {
      std::string row = std::string(kIndent) + '[';
      row += text::pad_left(std::to_string(current + 1), digits(total));
      row += " / " + std::to_string(total) + "] token " +
             std::to_string(token.index + 1);
      return row;
    }

    const std::string label = sub == 2 ? "read  : " : "expect: ";
    const std::string &value =
        sub == 2 ? token.output_token : token.expect_token;
    const std::string shown = value.empty() ? std::string("null") : value;
    const std::size_t room = state.width > kDetailIndent + label.size()
                                 ? state.width - kDetailIndent - label.size()
                                 : 1;
    return std::string(kIndent) + label + ellipsize(shown, room);
  }

  const std::size_t row_index = sub - 1;
  if (row_index < node.detail.size())
    return std::string(kIndent) + node.detail[row_index];
  return {};
}

std::string status_of(const CompareResult &result) {
  std::string text = result.exact_match ? "MATCH" : "MISMATCH";
  if (!result.exact_match)
    text += "   " + std::to_string(result.unmatched_line_count) +
            " lines unmatched";
  text += "   (output " + std::to_string(result.output_line_count) +
          ", expect " + std::to_string(result.expect_line_count) + ")";
  return text;
}

} // namespace

std::size_t node_height(const Node &node) noexcept {
  if (!node.expanded)
    return 1;
  return 1 + (node.token_case ? 3 : node.detail.size());
}

State make_state(const CompareResult &result, std::size_t width) {
  State state;
  state.width = fit_width(width);
  state.header = header_rows(result);
  state.nodes.reserve(result.unmatched_lines.size());
  for (const LineDiff &diff : result.unmatched_lines) {
    Node node;
    node.diff = &diff;
    node.token_case = diff.kind == LineKind::Differ && !diff.whitespace_only &&
                      !diff.token_diffs.empty();
    if (diff.kind == LineKind::OnlyOutput)
      node.marker = '+';
    else if (diff.kind == LineKind::OnlyExpect)
      node.marker = '-';
    else
      node.marker = diff.whitespace_only ? '~' : '!';
    state.nodes.push_back(std::move(node));
  }
  return state;
}

void reshape(State &state, std::size_t width, std::size_t height) {
  const std::size_t next = fit_width(width);
  if (next != state.width) {
    state.width = next;
    for (Node &node : state.nodes) {
      if (node.expanded && !node.token_case)
        build_detail(state, node);
    }
  }
  clamp_to_cursor(state, height);
}

void apply(State &state, term::Key key, std::size_t height) {
  if (state.nodes.empty())
    return;

  Node &node = state.nodes[state.cursor];
  const std::size_t total = node.token_case ? node.diff->token_diffs.size() : 0;
  bool follow_cursor = true;

  switch (key) {
  case term::Key::Up:
    if (state.cursor > 0)
      --state.cursor;
    break;
  case term::Key::Down:
    if (state.cursor + 1 < state.nodes.size())
      ++state.cursor;
    break;
  case term::Key::ViewUp:
    if (state.top > 0)
      --state.top;
    follow_cursor = false;
    break;
  case term::Key::ViewDown:
    ++state.top;
    follow_cursor = false;
    break;
  case term::Key::Right:
  case term::Key::Enter:
    if (!node.expanded)
      expand(state, node);
    else if (node.token_case)
      node.token = std::min(node.token + 1, total - 1);
    else
      collapse(node);
    break;
  case term::Key::Left:
  case term::Key::ShiftEnter:
    if (!node.expanded)
      return;
    if (node.token == 0 && key == term::Key::Left)
      return;
    if (node.token_case && node.token > 0)
      --node.token;
    else
      collapse(node);
    break;
  case term::Key::Collapse:
    if (node.expanded)
      collapse(node);
    break;
  case term::Key::CollapseAll:
    for (Node &other : state.nodes)
      collapse(other);
    break;
  case term::Key::Quit:
  case term::Key::Resize:
  case term::Key::Backspace:
  case term::Key::Text:
    return;
  }

  if (follow_cursor)
    clamp_to_cursor(state, height);
  else
    clamp_range(state, height);
}

std::string render(const CompareResult &result, const State &state,
                   std::size_t height, std::string_view title) {
  const std::size_t visible = list_rows(height, state.header);

  std::string frame = "\x1b[H";
  std::size_t used = 0;

  auto put = [&](const std::string &content, bool highlight) {
    if (used >= height)
      return;
    std::string row = clip(content, state.width);
    if (highlight) {
      const std::size_t columns = text::display_width(row);
      frame += "\x1b[7m" + row;
      if (columns < state.width)
        frame += std::string(state.width - columns, ' ');
      frame += "\x1b[0m";
    } else {
      frame += row + "\x1b[K";
    }
    ++used;
    if (used < height)
      frame += "\r\n";
  };

  std::string status = status_of(result);
  if (!title.empty())
    status = std::string(title) + "   " + status;
  put(status, false);
  if (!result.warning.empty())
    put("warning: " + result.warning, false);

  std::size_t row = 0;
  std::size_t emitted = 0;
  for (std::size_t i = 0; i < state.nodes.size() && emitted < visible; ++i) {
    const Node &node = state.nodes[i];
    const std::size_t node_rows = node_height(node);
    if (row + node_rows <= state.top) {
      row += node_rows;
      continue;
    }
    for (std::size_t sub = 0; sub < node_rows; ++sub, ++row) {
      if (row < state.top)
        continue;
      if (emitted >= visible)
        break;
      put(node_row(state, node, i, sub), i == state.cursor && sub == 0);
      ++emitted;
    }
  }

  while (used < height)
    put(std::string(), false);

  return frame;
}

int view(const CompareResult &result) {
  term::Session session;
  std::string error;
  if (!term::Session::open(session, error)) {
    std::cerr << color::err("[view] ", error) << "\n";
    return 1;
  }

  term::Size size = session.size();
  State state = make_state(result, size.columns);

  for (;;) {
    session.write(render(result, state, size.rows));

    term::Key key{};
    if (!session.read(key))
      break;
    if (key == term::Key::Quit)
      break;
    if (key == term::Key::Resize) {
      size = session.size();
      reshape(state, size.columns, size.rows);
      continue;
    }
    apply(state, key, size.rows);
  }

  session.close();
  return 0;
}

namespace {

std::string case_label(const Case &item) {
  switch (item.state) {
  case CaseState::Identical:
    return "matched";
  case CaseState::Differ:
    return "differ";
  case CaseState::NoOutput:
    return "no output";
  case CaseState::NoAnswer:
    return "no answer";
  case CaseState::Unreadable:
    return "unreadable";
  case CaseState::Failed:
    return "failed";
  }
  return "";
}

std::string case_count(const Case &item) {
  if (item.state != CaseState::Differ)
    return {};
  return std::to_string(item.result.unmatched_line_count) + " lines";
}

std::string batch_summary(const std::vector<Case> &cases) {
  std::size_t differ = 0;
  std::size_t missing = 0;
  for (const Case &item : cases) {
    if (item.state == CaseState::Differ)
      ++differ;
    else if (item.state != CaseState::Identical)
      ++missing;
  }

  std::string out = std::to_string(cases.size());
  out += cases.size() == 1 ? " test case" : " test cases";
  if (differ != 0)
    out += ", " + std::to_string(differ) + " differ";
  if (missing != 0)
    out += ", " + std::to_string(missing) + " without a result";
  if (differ == 0 && missing == 0)
    out += ", all matched";
  return out;
}

std::string case_row(const std::vector<Case> &cases, std::size_t index,
                     std::size_t w_index, std::size_t w_name,
                     std::size_t w_status, std::size_t w_count) {
  const Case &item = cases[index];

  std::string row = text::pad_left(std::to_string(index + 1), w_index);
  row += "  ";
  row += text::pad_right(item.name, w_name);
  row += "  ";
  row += text::pad_right(case_label(item), w_status);

  const std::string count = case_count(item);
  if (!count.empty()) {
    row += "  ";
    row += text::pad_left(count, w_count);
  }
  return row;
}

void clamp_batch_range(BatchState &state, const std::vector<Case> &cases,
                       std::size_t height) {
  const std::size_t visible = list_rows(height, state.header);
  const std::size_t total = cases.size();
  state.top = std::min(state.top, total > visible ? total - visible : 0);
}

void clamp_batch(BatchState &state, const std::vector<Case> &cases,
                 std::size_t height) {
  const std::size_t visible = list_rows(height, state.header);
  if (visible != 0) {
    if (state.cursor < state.top)
      state.top = state.cursor;
    else if (state.cursor >= state.top + visible)
      state.top = state.cursor - visible + 1;
  }
  clamp_batch_range(state, cases, height);
}

} // namespace

bool case_browsable(const Case &item) noexcept {
  return item.state == CaseState::Identical || item.state == CaseState::Differ;
}

BatchState make_batch_state(std::size_t width) {
  BatchState state;
  state.width = fit_width(width);
  return state;
}

void reshape_batch(BatchState &state, const std::vector<Case> &cases,
                   std::size_t width, std::size_t height) {
  state.width = fit_width(width);
  if (state.entered >= 0)
    reshape(state.inner, state.width, height);
  clamp_batch(state, cases, height);
}

void apply_batch(BatchState &state, const std::vector<Case> &cases,
                 term::Key key, std::size_t height) {
  if (cases.empty())
    return;

  if (state.entered >= 0) {
    if (key == term::Key::Backspace) {
      state.entered = -1;
      state.inner = State{};
      clamp_batch(state, cases, height);
      return;
    }
    apply(state.inner, key, height);
    return;
  }

  switch (key) {
  case term::Key::Up:
    if (state.cursor > 0)
      --state.cursor;
    break;
  case term::Key::Down:
    if (state.cursor + 1 < cases.size())
      ++state.cursor;
    break;
  case term::Key::ViewUp:
    if (state.top > 0)
      --state.top;
    clamp_batch_range(state, cases, height);
    return;
  case term::Key::ViewDown:
    ++state.top;
    clamp_batch_range(state, cases, height);
    return;
  case term::Key::Enter:
  case term::Key::Right:
    if (case_browsable(cases[state.cursor])) {
      state.inner = make_state(cases[state.cursor].result, state.width);
      state.entered = static_cast<std::ptrdiff_t>(state.cursor);
    }
    break;
  default:
    return;
  }

  clamp_batch(state, cases, height);
}

std::string render_batch(const std::vector<Case> &cases,
                         const BatchState &state, std::size_t height) {
  std::string frame = "\x1b[H";
  std::size_t used = 0;

  auto put = [&](const std::string &content, bool highlight) {
    if (used >= height)
      return;
    std::string row = clip(content, state.width);
    if (highlight) {
      const std::size_t columns = text::display_width(row);
      frame += "\x1b[7m" + row;
      if (columns < state.width)
        frame += std::string(state.width - columns, ' ');
      frame += "\x1b[0m";
    } else {
      frame += row + "\x1b[K";
    }
    ++used;
    if (used < height)
      frame += "\r\n";
  };

  put(batch_summary(cases), false);

  std::size_t w_index = digits(cases.size());
  std::size_t w_name = 0;
  std::size_t w_status = 0;
  std::size_t w_count = 0;
  for (const Case &item : cases) {
    w_name = std::max(w_name, text::display_width(item.name));
    w_status = std::max(w_status, text::display_width(case_label(item)));
    w_count = std::max(w_count, text::display_width(case_count(item)));
  }

  for (std::size_t i = state.top; i < cases.size() && used < height; ++i)
    put(case_row(cases, i, w_index, w_name, w_status, w_count),
        i == state.cursor);

  while (used < height)
    put(std::string(), false);

  return frame;
}

int view_batch(const std::vector<Case> &cases) {
  term::Session session;
  std::string error;
  if (!term::Session::open(session, error)) {
    std::cerr << color::err("[view] ", error) << "\n";
    return 1;
  }

  term::Size size = session.size();
  BatchState state = make_batch_state(size.columns);

  for (;;) {
    if (state.entered >= 0) {
      const Case &item = cases[static_cast<std::size_t>(state.entered)];
      session.write(render(item.result, state.inner, size.rows, item.name));
    } else {
      session.write(render_batch(cases, state, size.rows));
    }

    term::Key key{};
    if (!session.read(key))
      break;
    if (key == term::Key::Quit)
      break;
    if (key == term::Key::Resize) {
      size = session.size();
      reshape_batch(state, cases, size.columns, size.rows);
      continue;
    }
    apply_batch(state, cases, key, size.rows);
  }

  session.close();
  return 0;
}

} // namespace viewer
