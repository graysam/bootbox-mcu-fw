#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include <stdio.h>
#include <vector>
// Qt core for Project/Parser
#include <BBBuilder/Project.h>
#include <BBBuilder/SigmaParser.h>
#include <BBBuilder/BundleWriter.h>
#include <filesystem>
#include <thread>
#include <atomic>
#include "tinyfiledialogs.h"
#include <QString>
#include <QFileInfo>

namespace BBIM { struct Document; void DrawCanvas(Document&); }
#include "imgui_model.h"

struct AppState {
    BBIM::Document* doc = nullptr;
    BBB::Project project;
    int currentAlgorithm = -1;
    QString importPath;
    QString status;
    QString projectPath;
    std::atomic<bool> importInFlight{false};
    std::atomic<bool> importReady{false};
    std::string importResultPath;
    float algPanelHeight = 220.0f; // adjustable splitter height for Algorithm Blocks panel
    // Filters
    char algFilter[128] = {0};
    char paramFilter[128] = {0};
};

static void layoutWindows() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 pos = vp->WorkPos;
    const ImVec2 size = vp->WorkSize;
    const float leftW = size.x * 0.22f;
    const float rightW = size.x * 0.25f;
    const float centerW = size.x - leftW - rightW - 6.0f;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(leftW, size.y));
    ImGui::Begin("Components", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW + 3.0f, pos.y));
    ImGui::SetNextWindowSize(ImVec2(centerW, size.y));
    ImGui::Begin("Canvas", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(pos.x + leftW + centerW + 6.0f, pos.y));
    ImGui::SetNextWindowSize(ImVec2(rightW, size.y));
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::End();
}

int main(int, char**) {
    if (!glfwInit()) return 1;
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "BB-Builder", NULL, NULL);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Docking disabled in this minimal build; static layout used
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    BBIM::Document doc;
    AppState app{ &doc };
    

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Sigma Folder…")) {
                if (!app.importInFlight.load()) {
                    app.importInFlight.store(true);
                    const std::string def = app.importPath.isEmpty() ? std::string("etc/Sigmastudio_sysfile_example") : app.importPath.toStdString();
                    app.status = QStringLiteral("Opening folder dialog…");
                    std::thread([&app, def]{
                        const char* sel = tinyfd_selectFolderDialog("Select Sigma Exports", def.c_str());
                        app.importResultPath = (sel && sel[0]) ? std::string(sel) : std::string();
                        app.importReady.store(true);
                        app.importInFlight.store(false);
                    }).detach();
                }
            }
            if (ImGui::MenuItem("Open Project…")) {
                ImGui::OpenPopup("OpenProjectDlg");
            }
            if (ImGui::MenuItem("Save Project As…")) {
                ImGui::OpenPopup("SaveProjectDlg");
            }
            if (ImGui::MenuItem("Export Bundle…")) {
                ImGui::OpenPopup("ExportBundleDlg");
            }
            ImGui::Separator();
            ImGui::MenuItem("Save", "Ctrl+S", false, false);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) { 
            bool canUndo = !app.doc->undoStack.empty();
            bool canRedo = !app.doc->redoStack.empty();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) {
                if (canUndo) {
                    auto cmd = app.doc->undoStack.back(); app.doc->undoStack.pop_back(); app.doc->apply(cmd, false); app.doc->redoStack.push_back(cmd);
                }
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, canRedo)) {
                if (canRedo) {
                    auto cmd = app.doc->redoStack.back(); app.doc->redoStack.pop_back(); app.doc->apply(cmd, true); app.doc->undoStack.push_back(cmd);
                }
            }
            ImGui::EndMenu(); 
        }
        ImGui::EndMainMenuBar();
    }

        layoutWindows();

        // Left panel (components)
        ImGui::Begin("Components");
        if (!app.status.isEmpty()) {
            ImGui::TextWrapped("%s", app.status.toUtf8().constData());
            ImGui::Separator();
        }
        // Import panel (button + folder chooser dialog)
        ImGui::TextUnformatted("Import Sigma Exports");
        if (ImGui::Button("Import…")) {
            if (!app.importInFlight.load()) {
                app.importInFlight.store(true);
                const std::string def = app.importPath.isEmpty() ? std::string("etc/Sigmastudio_sysfile_example") : app.importPath.toStdString();
                app.status = QStringLiteral("Opening folder dialog…");
                std::thread([&app, def]{
                    const char* sel = tinyfd_selectFolderDialog("Select Sigma Exports", def.c_str());
                    app.importResultPath = (sel && sel[0]) ? std::string(sel) : std::string();
                    app.importReady.store(true);
                    app.importInFlight.store(false);
                }).detach();
            }
        }

        // Handle async dialog results
        if (app.importReady.load()) {
            app.importReady.store(false);
            if (!app.importResultPath.empty()) {
                BBB::SigmaParser parser;
                auto res = parser.parseFromPath(QString::fromStdString(app.importResultPath));
                if (res) {
                    app.project.clear();
                    app.project.modules() = res->modules;
                    app.project.setAlgorithms(res->algorithms);
                    app.project.setProgramBinaryPath(res->programBinaryPath);
                    app.importPath = QString::fromStdString(app.importResultPath);
                    app.currentAlgorithm = app.project.algorithms().isEmpty() ? -1 : 0;
                    app.status = QStringLiteral("Imported %1 modules from %2")
                                     .arg(app.project.modules().size())
                                     .arg(QFileInfo(app.importPath).fileName());
                } else {
                    app.status = QStringLiteral("Import failed: check folder contents");
                }
            } else {
                app.status = QStringLiteral("Import canceled or dialog unavailable");
            }
        }
        ImGui::Separator();
        // Compute available height for split panels
        float availY = ImGui::GetContentRegionAvail().y;
        const float minPanel = 100.0f;
        const float splitterH = 6.0f;
        if (app.algPanelHeight < minPanel) app.algPanelHeight = minPanel;
        if (app.algPanelHeight > availY - minPanel - splitterH) app.algPanelHeight = std::max(minPanel, availY - minPanel - splitterH);

        // Algorithm Blocks panel (scrollable)
        ImGui::BeginChild("AlgPanel", ImVec2(0, app.algPanelHeight), true);
        ImGui::TextUnformatted("Algorithm Blocks");
        ImGui::Separator();
        ImGui::InputText("Filter##alg", app.algFilter, sizeof(app.algFilter));
        for (int i = 0; i < app.project.algorithms().size(); ++i) {
            ImGui::PushID(i);
            const auto &alg = app.project.algorithms()[i];
            std::string label = (alg.friendlyName.isEmpty() ? alg.cellName : alg.friendlyName).toStdString();
            if (app.algFilter[0]) {
                std::string f = app.algFilter; for (auto &c:f) c = (char)tolower(c);
                std::string l = label; for (auto &c:l) c = (char)tolower(c);
                if (l.find(f) == std::string::npos) { ImGui::PopID(); continue; }
            }
            if (ImGui::Selectable(label.c_str(), app.currentAlgorithm == i)) {
                app.currentAlgorithm = i;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoDisableHover)) {
                std::string module = alg.moduleName.toStdString();
                ImGui::SetDragDropPayload("NEW_BLOCK", module.c_str(), module.size()+1);
                ImGui::Text("Create Module: %s", module.c_str());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        // Splitter: draggable bar to adjust Alg panel height
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f,0.2f,0.35f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f,0.35f,0.60f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f,0.45f,0.75f,1.0f));
        ImGui::Button("##Splitter", ImVec2(-FLT_MIN, splitterH));
        if (ImGui::IsItemActive()) {
            app.algPanelHeight += ImGui::GetIO().MouseDelta.y;
            if (app.algPanelHeight < minPanel) app.algPanelHeight = minPanel;
            if (app.algPanelHeight > availY - minPanel - splitterH) app.algPanelHeight = std::max(minPanel, availY - minPanel - splitterH);
        }
        ImGui::PopStyleColor(3);

        // Assignable Params panel (fills remaining space; independent scroll)
        ImGui::BeginChild("ParamPanel", ImVec2(0, 0), true);
        ImGui::TextUnformatted("Assignable Params");
        ImGui::Separator();
        ImGui::InputText("Filter##param", app.paramFilter, sizeof(app.paramFilter));
        if (app.currentAlgorithm >= 0 && app.currentAlgorithm < app.project.algorithms().size()) {
            const auto &alg = app.project.algorithms()[app.currentAlgorithm];
            int pidx = 0;
            for (const auto &cid : alg.controlIds) {
                ImGui::PushID(pidx++);
                const auto *ctrl = app.project.findControl(alg.moduleName, cid);
                if (!ctrl) { ImGui::PopID(); continue; }
                std::string label = ctrl->label.toStdString();
                if (app.paramFilter[0]) {
                    std::string f = app.paramFilter; for (auto &c:f) c = (char)tolower(c);
                    std::string l = label; for (auto &c:l) c = (char)tolower(c);
                    if (l.find(f) == std::string::npos) { ImGui::PopID(); continue; }
                }
                if (ImGui::Selectable(label.c_str())) {}
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoDisableHover)) {
                    struct NewParamPayload { char module[96]; char control[128]; } np{};
                    auto m = alg.moduleName.toUtf8(); auto c = cid.toUtf8();
                    snprintf(np.module, sizeof(np.module), "%s", m.constData());
                    snprintf(np.control, sizeof(np.control), "%s", c.constData());
                    ImGui::SetDragDropPayload("NEW_PARAM", &np, sizeof(np));
                    ImGui::Text("Add Param: %s", np.control);
                    ImGui::EndDragDropSource();
                }
                ImGui::PopID();
            }
        } else {
            ImGui::TextDisabled("Select an algorithm above");
        }
        ImGui::EndChild();
        ImGui::End();

        // Canvas center
        ImGui::Begin("Canvas");
        BBIM::DrawCanvas(*app.doc);
        ImGui::End();

        // Right panel inspector
        ImGui::Begin("Inspector");
        ImGui::TextUnformatted("Properties");
        ImGui::Separator();
        auto* sel = app.doc->get(app.doc->selection);
        if (!sel) {
            ImGui::TextDisabled("No selection");
        } else {
            // Common
            char labelBuf[128] = {0};
            snprintf(labelBuf, sizeof(labelBuf), "%s", sel->label.c_str());
            if (ImGui::InputText("Label", labelBuf, sizeof(labelBuf))) {
                sel->label = labelBuf;
            }
            ImGui::Text("Type: %s", sel->type == BBIM::WidgetType::ModuleCard ? "ModuleCard" : "Control");
            if (sel->type == BBIM::WidgetType::ModuleCard) {
                ImGui::Text("Module: %s", sel->moduleName.c_str());
            } else {
                ImGui::Text("Module: %s", sel->moduleName.c_str());
                ImGui::Text("Param: %s", sel->controlId.c_str());
                // Remap dropdown scoped to module
                if (!sel->moduleName.empty()) {
                    const QString qmod = QString::fromStdString(sel->moduleName);
                    const auto* mod = app.project.findModule(qmod);
                    if (mod) {
                        int currentIdx = -1;
                        std::vector<std::string> options;
                        for (int i = 0; i < mod->controls.size(); ++i) {
                            const auto &c = mod->controls[i];
                            options.push_back(c.id.toStdString());
                            if (c.id.toStdString() == sel->controlId) currentIdx = i;
                        }
                        if (ImGui::BeginCombo("Remap Param", currentIdx >= 0 ? options[currentIdx].c_str() : "<none>")) {
                            for (int i = 0; i < (int)options.size(); ++i) {
                                bool selected = (i == currentIdx);
                                if (ImGui::Selectable(options[i].c_str(), selected)) { BBIM::Document::Command c; c.kind = BBIM::Document::Command::Remap; c.id = app.doc->selection; c.modBefore = sel->moduleName; c.ctrlBefore = sel->controlId; c.modAfter = sel->moduleName; c.ctrlAfter = options[i]; app.doc->pushAndApply(c);} 
                                if (selected) ImGui::SetItemDefaultFocus();
                            }
                            ImGui::EndCombo();
                        }
                    }
                }
            }
            // Geometry and actions
            ImGui::Separator();
            ImGui::TextUnformatted("Geometry");
            float gx = sel->pos.x, gy = sel->pos.y, gw = sel->size.x, gh = sel->size.y;
            if (ImGui::DragFloat("X", &gx, 1.0f)) sel->pos.x = app.doc->snap ? std::round(gx / app.doc->grid) * app.doc->grid : gx;
            if (ImGui::DragFloat("Y", &gy, 1.0f)) sel->pos.y = app.doc->snap ? std::round(gy / app.doc->grid) * app.doc->grid : gy;
            if (ImGui::DragFloat("W", &gw, 1.0f, 40.0f, 1200.0f)) sel->size.x = std::max(20.0f, gw);
            if (ImGui::DragFloat("H", &gh, 1.0f, 24.0f, 1200.0f)) sel->size.y = std::max(20.0f, gh);
            if (ImGui::Button("Duplicate")) { int nid = app.doc->duplicateWidget(sel->id); if (auto* nw = app.doc->get(nid)) { nw->pos.x += 16; nw->pos.y += 16; BBIM::Document::Command c; c.kind = BBIM::Document::Command::Create; c.id = nid; app.doc->pushAndApply(c);} }
            ImGui::SameLine(); if (ImGui::Button("Bring Front")) app.doc->bringToFront(sel->id);
            ImGui::SameLine(); if (ImGui::Button("Send Back")) app.doc->sendToBack(sel->id);
            if (ImGui::Button("Delete")) { BBIM::Document::Command c; c.kind = BBIM::Document::Command::Delete; c.id = sel->id; app.doc->pushAndApply(c); app.doc->selection = -1; }
        }
        ImGui::End();

        // Global key handling (delete/undo/redo + arrow nudge)
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && app.doc->selection >= 0) {
            auto* selw = app.doc->get(app.doc->selection);
            if (selw) { 
                BBIM::Document::Command cmd; cmd.kind = BBIM::Document::Command::Delete; cmd.id = selw->id; app.doc->pushAndApply(cmd);
            }
            app.doc->selection = -1;
        }
        bool ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (!app.doc->undoStack.empty()) { auto cmd = app.doc->undoStack.back(); app.doc->undoStack.pop_back(); app.doc->apply(cmd, false); app.doc->redoStack.push_back(cmd);} }
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Y)) {
            if (!app.doc->redoStack.empty()) { auto cmd = app.doc->redoStack.back(); app.doc->redoStack.pop_back(); app.doc->apply(cmd, true); app.doc->undoStack.push_back(cmd);} }

        // Arrow nudge
        if (app.doc->selection >= 0) {
            auto* w = app.doc->get(app.doc->selection);
            if (w) {
                ImVec2 before = w->pos;
                float step = (ImGui::GetIO().KeyShift ? 5.0f : 1.0f) * (app.doc->snap ? app.doc->grid : 1.0f);
                bool moved = false;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { w->pos.x -= step; moved = true; }
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { w->pos.x += step; moved = true; }
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { w->pos.y -= step; moved = true; }
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { w->pos.y += step; moved = true; }
                if (moved) { BBIM::Document::Command c; c.kind = BBIM::Document::Command::Move; c.id = w->id; c.posBefore = before; c.posAfter = w->pos; app.doc->pushAndApply(c); }
            }
        }

        // (Popup-based Import removed; using native folder dialog)

        // Save Project dialog
        if (ImGui::BeginPopupModal("SaveProjectDlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char saveBuf[512] = {0};
            if (saveBuf[0] == '\0' && app.projectPath.isEmpty()) {
                snprintf(saveBuf, sizeof(saveBuf), "%s", "BB-Builder/example.bbproj");
            }
            ImGui::InputText("Path", saveBuf, sizeof(saveBuf));
            if (ImGui::Button("Save")) {
                // Serialize canvas into project canvas JSON
                QJsonArray canvas;
                for (const auto &w : app.doc->widgets) {
                    if (!w.alive) continue;
                    QJsonObject o;
                    o.insert("id", w.id);
                    o.insert("parent", w.parent);
                    o.insert("type", (int)w.type);
                    QJsonArray pos; pos.append(w.pos.x); pos.append(w.pos.y); o.insert("pos", pos);
                    QJsonArray size; size.append(w.size.x); size.append(w.size.y); o.insert("size", size);
                    o.insert("label", QString::fromStdString(w.label));
                    o.insert("module", QString::fromStdString(w.moduleName));
                    o.insert("control", QString::fromStdString(w.controlId));
                    QJsonArray kids; for (int cid : w.children) kids.append(cid); o.insert("children", kids);
                    canvas.append(o);
                }
                app.project.setCanvas(canvas);
                // write editor settings
                app.project.editor().algPanelHeight = app.algPanelHeight;
                app.project.editor().snap = app.doc->snap;
                app.project.editor().grid = app.doc->grid;
                app.project.editor().zoom = app.doc->zoom;
                app.project.editor().panX = app.doc->pan.x;
                app.project.editor().panY = app.doc->pan.y;
                QString err;
                if (app.project.saveToFile(QString::fromUtf8(saveBuf), &err)) {
                    app.projectPath = QString::fromUtf8(saveBuf);
                    app.status = QStringLiteral("Saved project → %1").arg(app.projectPath);
                } else {
                    app.status = QStringLiteral("Save failed: %1").arg(err);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Open Project dialog
        if (ImGui::BeginPopupModal("OpenProjectDlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char openBuf[512] = {0};
            ImGui::InputText("Path", openBuf, sizeof(openBuf));
            if (ImGui::Button("Open")) {
                QString err;
                BBB::Project p;
                if (p.loadFromFile(QString::fromUtf8(openBuf), &err)) {
                    app.project = p;
                    app.projectPath = QString::fromUtf8(openBuf);
                    // Rehydrate canvas
                    app.doc->widgets.clear();
                    app.doc->selection = -1;
                    auto canvas = app.project.canvas();
                    app.doc->widgets.reserve(canvas.size());
                    for (const auto &v : canvas) {
                        if (!v.isObject()) continue;
                        const auto o = v.toObject();
                        BBIM::Widget w;
                        w.id = o.value("id").toInt();
                        w.parent = o.value("parent").toInt(-1);
                        w.alive = true;
                        w.type = (BBIM::WidgetType)o.value("type").toInt();
                        auto pos = o.value("pos").toArray(); w.pos = ImVec2(pos.size()>0?pos.at(0).toDouble(0.0):0.0, pos.size()>1?pos.at(1).toDouble(0.0):0.0);
                        auto size = o.value("size").toArray(); w.size = ImVec2(size.size()>0?size.at(0).toDouble(0.0):0.0, size.size()>1?size.at(1).toDouble(0.0):0.0);
                        w.label = o.value("label").toString().toStdString();
                        w.moduleName = o.value("module").toString().toStdString();
                        w.controlId = o.value("control").toString().toStdString();
                        auto kids = o.value("children").toArray(); for (const auto &k : kids) w.children.push_back(k.toInt());
                        app.doc->widgets.push_back(w);
                    }
                    // apply editor settings
                    app.algPanelHeight = (float)app.project.editor().algPanelHeight;
                    app.doc->snap = app.project.editor().snap;
                    app.doc->grid = (float)app.project.editor().grid;
                    app.doc->zoom = (float)app.project.editor().zoom;
                    app.doc->pan = ImVec2((float)app.project.editor().panX, (float)app.project.editor().panY);
                    app.status = QStringLiteral("Opened project ← %1").arg(app.projectPath);
                    app.currentAlgorithm = app.project.algorithms().isEmpty() ? -1 : 0;
                } else {
                    app.status = QStringLiteral("Open failed: %1").arg(err);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Export Bundle dialog
        if (ImGui::BeginPopupModal("ExportBundleDlg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static char outBuf[512] = {0};
            if (outBuf[0] == '\0') snprintf(outBuf, sizeof(outBuf), "%s", "bundle.bbx");
            ImGui::InputText("Output", outBuf, sizeof(outBuf));
            if (ImGui::Button("Export")) {
                // Ensure canvas JSON is stored
                QJsonArray canvas;
                for (const auto &w : app.doc->widgets) {
                    if (!w.alive) continue;
                    QJsonObject o;
                    o.insert("id", w.id);
                    o.insert("parent", w.parent);
                    o.insert("type", (int)w.type);
                    QJsonArray pos; pos.append(w.pos.x); pos.append(w.pos.y); o.insert("pos", pos);
                    QJsonArray size; size.append(w.size.x); size.append(w.size.y); o.insert("size", size);
                    o.insert("label", QString::fromStdString(w.label));
                    o.insert("module", QString::fromStdString(w.moduleName));
                    o.insert("control", QString::fromStdString(w.controlId));
                    QJsonArray kids; for (int cid : w.children) kids.append(cid); o.insert("children", kids);
                    canvas.append(o);
                }
                app.project.setCanvas(canvas);
                BBB::BundleWriter writer;
                QString err;
                BBB::BundleWriter::Options opt; opt.dryRun = false;
                if (writer.writeBundle(app.project, QString::fromUtf8(outBuf), opt)) {
                    app.status = QStringLiteral("Exported → %1").arg(QString::fromUtf8(outBuf));
                } else {
                    app.status = QStringLiteral("Export failed: %1").arg(writer.lastError());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // (Push to Device dialog removed for now)

        ImGui::Render();
        int display_w, display_h; glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.04f,0.07f,0.12f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
