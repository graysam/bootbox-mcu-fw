#pragma once
#include <imgui.h>
#include <vector>
#include <string>

namespace BBIM {
enum class WidgetType { ModuleCard, Label, Knob, Slider, Toggle, Meter };

struct Widget {
    int id = 0;
    int parent = -1;               // -1 if root (e.g., ModuleCard)
    bool alive = true;
    WidgetType type = WidgetType::ModuleCard;
    ImVec2 pos = {50,50};          // world for roots, parent-local for children
    ImVec2 size = {220,140};
    std::string label;
    // Binding info (for parameter-backed controls)
    std::string moduleName;        // Sigma module name (cell)
    std::string controlId;         // ControlDescriptor::id
    std::vector<int> children;     // indices into Document::widgets
};

struct Document {
    std::vector<Widget> widgets;
    int selection = -1;
    float zoom = 1.0f;
    ImVec2 pan = {0,0};
    bool snap = true;
    float grid = 12.0f;

    int addWidget(WidgetType t, ImVec2 pos, int parent = -1);
    Widget* get(int id);
    const Widget* get(int id) const;
    int topMostAt(const ImVec2& screenPos, const ImVec2& origin, const ImVec2& avail) const;

    // Commands (undo/redo)
    struct Command {
        enum Kind { Create, Delete, Move, Rename, Remap } kind;
        int id = -1;            // widget id
        ImVec2 posBefore{0,0}, posAfter{0,0};
        std::string labelBefore, labelAfter;
        std::string modBefore, modAfter;
        std::string ctrlBefore, ctrlAfter;
        bool applied = false;
    };
    std::vector<Command> undoStack;
    std::vector<Command> redoStack;
    void apply(const Command &cmd, bool forward);
    void pushAndApply(Command cmd);

    // Utilities for editing
    int duplicateWidget(int id);
    void bringToFront(int id);
    void sendToBack(int id);
};
}
