#include "imgui_model.h"
#include <cmath>

namespace BBIM {

int Document::addWidget(WidgetType t, ImVec2 p, int parent) {
    Widget w; w.id = (int)widgets.size(); w.type = t; w.pos = p; w.parent = parent; w.label = "";
    if (t == WidgetType::ModuleCard) w.size = ImVec2(320, 220);
    widgets.push_back(w);
    if (parent >= 0 && parent < (int)widgets.size()) {
        widgets[parent].children.push_back(w.id);
    }
    return w.id;
}

Widget* Document::get(int id) {
    if (id < 0 || id >= (int)widgets.size()) return nullptr;
    if (!widgets[id].alive) return nullptr;
    return &widgets[id];
}

const Widget* Document::get(int id) const {
    if (id < 0 || id >= (int)widgets.size()) return nullptr;
    if (!widgets[id].alive) return nullptr;
    return &widgets[id];
}

int Document::topMostAt(const ImVec2& screenPos, const ImVec2& origin, const ImVec2& avail) const {
    // Iterate back-to-front: later widgets are drawn on top
    for (int i = (int)widgets.size() - 1; i >= 0; --i) {
        const Widget& w = widgets[i];
        if (!w.alive) continue;
        // Compute rect in screen coords
        ImVec2 pos = w.pos;
        if (w.parent >= 0 && w.parent < (int)widgets.size()) {
            const Widget& p = widgets[w.parent];
            pos = ImVec2(p.pos.x + w.pos.x, p.pos.y + w.pos.y); // parent world + local
        }
        ImVec2 a = ImVec2(origin.x + pan.x + pos.x * zoom, origin.y + pan.y + pos.y * zoom);
        ImVec2 b = ImVec2(a.x + w.size.x * zoom, a.y + w.size.y * zoom);
        if (screenPos.x >= a.x && screenPos.x <= b.x && screenPos.y >= a.y && screenPos.y <= b.y) {
            return i;
        }
    }
    return -1;
}

static ImVec2 snapped(ImVec2 v, float g) {
    return ImVec2(std::round(v.x / g) * g, std::round(v.y / g) * g);
}

void Document::apply(const Command &cmdIn, bool forward) {
    Command cmd = cmdIn;
    Widget* w = get(cmd.id);
    switch (cmd.kind) {
        case Command::Create:
            if (w) { w->alive = forward; }
            break;
        case Command::Delete:
            if (w) { w->alive = !forward; }
            break;
        case Command::Move:
            if (w) {
                w->pos = forward ? cmd.posAfter : cmd.posBefore;
            }
            break;
        case Command::Rename:
            if (w) {
                w->label = forward ? cmd.labelAfter : cmd.labelBefore;
            }
            break;
        case Command::Remap:
            if (w) {
                w->moduleName = forward ? cmd.modAfter : cmd.modBefore;
                w->controlId  = forward ? cmd.ctrlAfter : cmd.ctrlBefore;
            }
            break;
    }
}

void Document::pushAndApply(Command cmd) {
    // snap move targets if enabled
    if (cmd.kind == Command::Move && snap && grid > 0.0f) {
        cmd.posAfter = snapped(cmd.posAfter, grid);
    }
    apply(cmd, true);
    undoStack.push_back(cmd);
    redoStack.clear();
}

int Document::duplicateWidget(int id) {
    const Widget* src = get(id);
    if (!src) return -1;
    Widget copy = *src;
    copy.id = (int)widgets.size();
    copy.children.clear();
    widgets.push_back(copy);
    // if parented, append to parent children list
    if (copy.parent >= 0 && copy.parent < (int)widgets.size()) {
        widgets[copy.parent].children.push_back(copy.id);
    }
    // deep copy children if module card
    if (src->type == WidgetType::ModuleCard) {
        for (int cid : src->children) {
            const Widget* child = get(cid);
            if (!child) continue;
            Widget cc = *child;
            cc.id = (int)widgets.size();
            cc.parent = copy.id;
            widgets.push_back(cc);
            widgets[copy.id].children.push_back(cc.id);
        }
    }
    return copy.id;
}

void Document::bringToFront(int id) {
    if (id < 0 || id >= (int)widgets.size()) return;
    // move selected widget to end for draw on top
    Widget w = widgets[id];
    widgets.erase(widgets.begin() + id);
    widgets.push_back(w);
}

void Document::sendToBack(int id) {
    if (id < 0 || id >= (int)widgets.size()) return;
    Widget w = widgets[id];
    widgets.erase(widgets.begin() + id);
    widgets.insert(widgets.begin(), w);
}

} // namespace BBIM
