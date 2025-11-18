#include <imgui.h>
#include <cmath>
#include <vector>
#include "imgui_model.h"

namespace BBIM {

struct NewParamPayload { char module[96]; char control[128]; };

static bool pointInRect(ImVec2 p, ImVec2 a, ImVec2 b) {
    return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
}

void DrawCanvas(Document &doc) {
    const float toolbarH = 34.0f;
    ImGui::BeginChild("CanvasToolbar", ImVec2(0, toolbarH), false);
    ImGui::TextUnformatted("Canvas");
    ImGui::SameLine(); if (ImGui::Button("+ Knob")) {
        auto* sel = doc.get(doc.selection);
        if (sel && sel->type == WidgetType::ModuleCard) {
            int cid = doc.addWidget(WidgetType::Knob, ImVec2(12, 40 + (float)sel->children.size() * 44.0f), sel->id);
            auto* c = doc.get(cid); if (c) { c->size = ImVec2(90, 90); c->label = "Knob"; }
        }
    }
    ImGui::SameLine(); if (ImGui::Button("+ Slider")) {
        auto* sel = doc.get(doc.selection);
        if (sel && sel->type == WidgetType::ModuleCard) {
            int cid = doc.addWidget(WidgetType::Slider, ImVec2(12, 40 + (float)sel->children.size() * 44.0f), sel->id);
            auto* c = doc.get(cid); if (c) { c->size = ImVec2(140, 36); c->label = "Slider"; }
        }
    }
    ImGui::SameLine(); if (ImGui::Button("+ Toggle")) {
        auto* sel = doc.get(doc.selection);
        if (sel && sel->type == WidgetType::ModuleCard) {
            int cid = doc.addWidget(WidgetType::Toggle, ImVec2(12, 40 + (float)sel->children.size() * 44.0f), sel->id);
            auto* c = doc.get(cid); if (c) { c->size = ImVec2(100, 28); c->label = "Toggle"; }
        }
    }
    ImGui::SameLine(); if (ImGui::Button("Zoom+")) doc.zoom = std::min(2.5f, doc.zoom + 0.1f);
    ImGui::SameLine(); if (ImGui::Button("Zoom-")) doc.zoom = std::max(0.25f, doc.zoom - 0.1f);
    ImGui::SameLine(); ImGui::Text("Zoom: %.2f", doc.zoom);
    ImGui::SameLine(0, 20.0f); if (ImGui::Button("Reset View")) { doc.zoom = 1.0f; doc.pan = ImVec2(0,0); }
    ImGui::SameLine(0, 20.0f);
    bool snap = doc.snap; if (ImGui::Checkbox("Snap", &snap)) doc.snap = snap;
    ImGui::SameLine(); ImGui::Text("Grid"); ImGui::SameLine();
    float g = doc.grid; if (ImGui::DragFloat("##grid", &g, 0.5f, 4.0f, 64.0f, "%.0f")) { doc.grid = g; }
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::BeginChild("CanvasViewport", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImU32 bg = IM_COL32(10,16,28,255);
    dl->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), bg, 8.0f);
    // grid
    const float grid = 24.0f * doc.zoom;
    for (float x = fmodf(doc.pan.x, grid); x < avail.x; x += grid) dl->AddLine(ImVec2(origin.x + x, origin.y), ImVec2(origin.x + x, origin.y + avail.y), IM_COL32(30,46,74,255));
    for (float y = fmodf(doc.pan.y, grid); y < avail.y; y += grid) dl->AddLine(ImVec2(origin.x, origin.y + y), ImVec2(origin.x + avail.x, origin.y + y), IM_COL32(30,46,74,255));

    // Pan with middle mouse
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        doc.pan.x += ImGui::GetIO().MouseDelta.x;
        doc.pan.y += ImGui::GetIO().MouseDelta.y;
    }
    // Zoom with Ctrl+wheel
    if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl) {
        float dz = ImGui::GetIO().MouseWheel * 0.1f;
        if (dz != 0.0f) doc.zoom = std::max(0.25f, std::min(3.0f, doc.zoom + dz));
    }

    // Drag and drop targets on canvas or widgets
    auto toWorld = [&](ImVec2 screen) {
        ImVec2 local = ImVec2(screen.x - origin.x - doc.pan.x, screen.y - origin.y - doc.pan.y);
        return ImVec2(local.x * (1.0f / doc.zoom), local.y * (1.0f / doc.zoom));
    };

    // Draw widgets
    int hoveredId = -1;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (size_t i = 0; i < doc.widgets.size(); ++i) {
        auto &w = doc.widgets[i];
        if (!w.alive) continue;
        ImVec2 pos = w.pos;
        if (w.parent >= 0) {
            const auto* p = doc.get(w.parent);
            if (p) pos = ImVec2(p->pos.x + w.pos.x, p->pos.y + w.pos.y);
        }
        ImVec2 a = ImVec2(origin.x + doc.pan.x + pos.x * doc.zoom, origin.y + doc.pan.y + pos.y * doc.zoom);
        ImVec2 b = ImVec2(a.x + w.size.x * doc.zoom, a.y + w.size.y * doc.zoom);
        ImU32 fill = (w.type == WidgetType::ModuleCard) ? IM_COL32(16,26,46,255) : IM_COL32(22,34,58,255);
        ImU32 border = IM_COL32(60,90,140,255);
        dl->AddRectFilled(a, b, fill, 8.0f);
        dl->AddRect(a, b, border, 8.0f);
        // Header for module cards
        if (w.type == WidgetType::ModuleCard) {
            ImVec2 hA = a;
            ImVec2 hB = ImVec2(b.x, a.y + 28.0f);
            dl->AddRectFilled(hA, hB, IM_COL32(14,22,40,255), 8.0f, ImDrawFlags_RoundCornersTop);
            dl->AddText(ImVec2(hA.x + 8.0f, hA.y + 6.0f), IM_COL32(200,220,255,255), w.label.empty() ? "Module" : w.label.c_str());
        } else {
            // Control title
            dl->AddText(ImVec2(a.x + 6.0f, a.y + 4.0f), IM_COL32(200,220,255,255), w.label.empty() ? "Control" : w.label.c_str());
            // Simple visual to resemble embedded UI controls
            ImVec2 ca = ImVec2(a.x + 8.0f, a.y + 22.0f);
            ImVec2 cb = ImVec2(b.x - 8.0f, b.y - 8.0f);
            if (w.type == WidgetType::Slider) {
                float midY = (ca.y + cb.y) * 0.5f;
                dl->AddRectFilled(ImVec2(ca.x, midY - 3), ImVec2(cb.x, midY + 3), IM_COL32(40,60,96,255), 3.0f);
                dl->AddRect(ImVec2(ca.x, midY - 3), ImVec2(cb.x, midY + 3), IM_COL32(70,100,150,255), 3.0f);
                float thumbX = ca.x + (cb.x - ca.x) * 0.6f; // placeholder position
                dl->AddCircleFilled(ImVec2(thumbX, midY), 7.0f, IM_COL32(180,200,255,255));
            } else if (w.type == WidgetType::Toggle) {
                float radius = (cb.y - ca.y) * 0.35f;
                float h = radius * 2.0f;
                ImVec2 ta = ImVec2(ca.x, ca.y + (cb.y - ca.y - h) * 0.5f);
                ImVec2 tb = ImVec2(ta.x + h * 2.0f, ta.y + h);
                dl->AddRectFilled(ta, tb, IM_COL32(40,70,110,255), h * 0.5f);
                dl->AddRect(ta, tb, IM_COL32(70,110,170,255), h * 0.5f);
                // knob (off)
                dl->AddCircleFilled(ImVec2(ta.x + h * 0.5f, ta.y + h * 0.5f), h * 0.42f, IM_COL32(180,200,255,255));
            } else if (w.type == WidgetType::Knob) {
                float r = std::min(cb.x - ca.x, cb.y - ca.y) * 0.45f;
                ImVec2 center = ImVec2((ca.x + cb.x) * 0.5f, (ca.y + cb.y) * 0.5f);
                dl->AddCircleFilled(center, r, IM_COL32(34,52,86,255));
                dl->AddCircle(center, r, IM_COL32(70,100,150,255));
                // indicator at ~300°
                float angle = 5.23599f; // 300deg
                ImVec2 p = ImVec2(center.x + cosf(angle) * (r - 6.0f), center.y + sinf(angle) * (r - 6.0f));
                dl->AddLine(center, p, IM_COL32(200,220,255,255), 2.0f);
            }
        }
        if (pointInRect(mouse, a, b)) hoveredId = (int)i;
    }

    // Selection & dragging
    static bool dragging = false;
    static ImVec2 dragOffset;
    static ImVec2 posBefore;
    if (ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            doc.selection = hoveredId;
            if (doc.selection >= 0) {
                // compute drag offset in local/world space
                auto* w = doc.get(doc.selection);
                if (w) {
                    ImVec2 pos = w->pos;
                    if (w->parent >= 0) {
                        auto* p = doc.get(w->parent);
                        if (p) pos = ImVec2(p->pos.x + w->pos.x, p->pos.y + w->pos.y);
                    }
                    ImVec2 world = toWorld(mouse);
                    dragOffset = ImVec2(world.x - pos.x, world.y - pos.y);
                    posBefore = w->pos;
                    dragging = true;
                }
            }
        }
        if (dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto* w = doc.get(doc.selection);
            if (w) {
                ImVec2 world = toWorld(mouse);
                ImVec2 newPos = ImVec2(world.x - dragOffset.x, world.y - dragOffset.y);
                if (w->parent >= 0) {
                    // store parent-local if child
                    auto* p = doc.get(w->parent);
                    if (p) newPos = ImVec2(newPos.x - p->pos.x, newPos.y - p->pos.y);
                }
                if (doc.snap && doc.grid > 0.0f) {
                    newPos.x = std::round(newPos.x / doc.grid) * doc.grid;
                    newPos.y = std::round(newPos.y / doc.grid) * doc.grid;
                }
                w->pos = newPos;
            }
        }
        if (dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            dragging = false;
            auto* w = doc.get(doc.selection);
            if (w) {
                Document::Command cmd; cmd.kind = Document::Command::Move; cmd.id = w->id; cmd.posBefore = posBefore; cmd.posAfter = w->pos; doc.pushAndApply(cmd); // re-applies snap normalization
            }
        }
    }

    // Inline rename: double-click selected opens rename popup
    static int renamingId = -1;
    static char renameBuf[128] = {0};
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (hoveredId >= 0) {
            renamingId = hoveredId;
            if (auto* w = doc.get(renamingId)) {
                snprintf(renameBuf, sizeof(renameBuf), "%s", w->label.c_str());
                ImGui::OpenPopup("RenameWidget");
            }
        }
    }
    if (ImGui::BeginPopup("RenameWidget")) {
        ImGui::InputText("##newname", renameBuf, sizeof(renameBuf));
        ImGui::SameLine();
        if (ImGui::Button("OK")) {
            if (auto* w = doc.get(renamingId)) { BBIM::Document::Command c; c.kind = BBIM::Document::Command::Rename; c.id = renamingId; c.labelBefore = w->label; c.labelAfter = std::string(renameBuf); doc.pushAndApply(c); }
            ImGui::CloseCurrentPopup();
            renamingId = -1;
        }
        ImGui::SameLine(); if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); renamingId = -1; }
        ImGui::EndPopup();
    }

    // Context menu for widgets
    if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        if (hoveredId >= 0) {
            doc.selection = hoveredId;
            ImGui::OpenPopup("WidgetMenu");
        } else {
            ImGui::OpenPopup("CanvasMenu");
        }
    }
    if (ImGui::BeginPopup("WidgetMenu")) {
        auto* w = doc.get(doc.selection);
        if (w) {
            if (ImGui::MenuItem("Duplicate")) {
                int nid = doc.duplicateWidget(w->id);
                if (auto* nw = doc.get(nid)) { nw->pos.x += 16; nw->pos.y += 16; BBIM::Document::Command c; c.kind = BBIM::Document::Command::Create; c.id = nid; doc.pushAndApply(c); }
            }
            if (ImGui::MenuItem("Delete")) {
                BBIM::Document::Command c; c.kind = BBIM::Document::Command::Delete; c.id = w->id; doc.pushAndApply(c);
            }
            if (ImGui::MenuItem("Bring to Front")) { doc.bringToFront(w->id); }
            if (ImGui::MenuItem("Send to Back")) { doc.sendToBack(w->id); }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("CanvasMenu")) {
        bool snap = doc.snap;
        if (ImGui::MenuItem(snap ? "Disable Snap" : "Enable Snap")) doc.snap = !doc.snap;
        ImGui::EndPopup();
    }

    // Full-surface drop target: create an invisible item so BeginDragDropTarget has a valid target
    ImVec2 savedCursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(0,0));
    ImGui::InvisibleButton("CanvasDropArea", avail);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("NEW_BLOCK")) {
            ImVec2 world = toWorld(mouse);
            const char* moduleName = (const char*)p->Data;
            int id = doc.addWidget(WidgetType::ModuleCard, world, -1);
            if (auto* w = doc.get(id)) { 
                w->label = moduleName ? moduleName : "Module"; w->moduleName = moduleName ? moduleName : ""; 
                Document::Command cmd; cmd.kind = Document::Command::Create; cmd.id = id; doc.pushAndApply(cmd);
            }
        }
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("NEW_PARAM")) {
            NewParamPayload np{}; memcpy(&np, p->Data, sizeof(np));
            int target = hoveredId;
            if (target >= 0) {
                auto* t = doc.get(target);
                int parent = -1;
                if (t && t->type == WidgetType::ModuleCard) parent = target;
                else if (t && t->parent >= 0) parent = t->parent;
                if (t && t->type != WidgetType::ModuleCard) {
                    // Remap existing control
                    BBIM::Document::Command c; c.kind = BBIM::Document::Command::Remap; c.id = t->id; c.modBefore = t->moduleName; c.ctrlBefore = t->controlId; c.modAfter = np.module; c.ctrlAfter = np.control; doc.pushAndApply(c);
                } else if (parent >= 0) {
                    int cid = doc.addWidget(WidgetType::Slider, ImVec2(12, 40 + (float)(doc.get(parent)->children.size()) * 44.0f), parent);
                    if (auto* c = doc.get(cid)) { c->label = np.control; c->moduleName = np.module; c->controlId = np.control; c->size = ImVec2(140, 36); BBIM::Document::Command cmd; cmd.kind = BBIM::Document::Command::Create; cmd.id = cid; doc.pushAndApply(cmd);} 
                }
            } else {
                ImVec2 world = toWorld(mouse);
                int id = doc.addWidget(WidgetType::ModuleCard, world, -1);
                if (auto* w = doc.get(id)) { w->label = np.module; w->moduleName = np.module; BBIM::Document::Command cmd; cmd.kind = BBIM::Document::Command::Create; cmd.id = id; doc.pushAndApply(cmd);} 
                int cid = doc.addWidget(WidgetType::Slider, ImVec2(12, 40), id);
                if (auto* c = doc.get(cid)) { c->label = np.control; c->moduleName = np.module; c->controlId = np.control; c->size = ImVec2(140, 36); BBIM::Document::Command cmd; cmd.kind = BBIM::Document::Command::Create; cmd.id = cid; doc.pushAndApply(cmd);} 
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SetCursorPos(savedCursor);

    ImGui::EndChild();
}

} // namespace BBIM
