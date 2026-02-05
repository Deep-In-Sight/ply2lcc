# Qt GUI for ply2lcc - Design Document

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a Qt-based GUI for users unfamiliar with CLI tools.

**Architecture:** Qt6 application linking ply2lcc_lib directly. Conversion runs in QThread with progress callback injection. UI stays responsive with disabled inputs during conversion.

**Tech Stack:** C++17, Qt6 Widgets, CMake

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Qt GUI Application                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │
│  │ File Pickers│  │ Settings    │  │ Progress Bar    │  │
│  │ - Input PLY │  │ - Cell X/Y  │  │ - 0-100%        │  │
│  │ - Output Dir│  │ - Single LOD│  │ - Status text   │  │
│  └─────────────┘  │ - Env/Coll  │  │ - Log box       │  │
│                   └─────────────┘  └─────────────────┘  │
│                         │                                │
│                    ┌────▼────┐                          │
│                    │ Convert │                          │
│                    │ Button  │                          │
│                    └────┬────┘                          │
│                         │ clicked                        │
│  ┌──────────────────────▼───────────────────────────┐   │
│  │              ConvertWorker (QThread)              │   │
│  │  - Runs ConvertApp with progress callback         │   │
│  │  - Emits progressChanged(int), logMessage(str)    │   │
│  └──────────────────────┬───────────────────────────┘   │
└─────────────────────────│───────────────────────────────┘
                          │ links
              ┌───────────▼───────────┐
              │     ply2lcc_lib       │
              │  (existing library)   │
              └───────────────────────┘
```

## GUI Layout

```
┌─────────────────────────────────────────────────────────┐
│  ply2lcc Converter                              [─][□][×]│
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Input PLY:    [________________________] [Browse...]   │
│                                                         │
│  Output Dir:   [________________________] [Browse...]   │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  Settings                                               │
│  ┌───────────────────────────────────────────────────┐  │
│  │  Cell Size X: [  30.0  ]  Cell Size Y: [  30.0  ] │  │
│  │                                                   │  │
│  │  ☐ Single LOD mode                                │  │
│  │  ☑ Include environment                            │  │
│  │  ☐ Include collision                              │  │
│  └───────────────────────────────────────────────────┘  │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  Log                                                    │
│  ┌───────────────────────────────────────────────────┐  │
│  │ [12:34:56] Loading point_cloud.ply...             │  │
│  │ [12:34:57] Found 2,451,823 splats                 │  │
│  │ [12:34:57] Building spatial grid...               │  │
│  │ [12:34:58] Encoding cell 12/27...                 │  │
│  └───────────────────────────────────────────────────┘  │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  [▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░░]  45%        │
│                                                         │
│                    [ Convert ]                          │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

## Progress Callback Interface

```cpp
// types.hpp
using ProgressCallback = std::function<void(int percent, const std::string& stage)>;

// ConvertApp accepts callback
class ConvertApp {
public:
    ConvertApp(const ConvertConfig& config);
    void setProgressCallback(ProgressCallback cb);
    void run();
};
```

**Progress stages:**
| Stage | Percent Range |
|-------|---------------|
| Reading PLY | 0-5% |
| Building Grid | 5-15% |
| Encoding | 15-90% |
| Writing | 90-100% |

## UI Behavior

- Browse buttons open native file/folder dialogs
- Convert button disabled until input/output are set
- During conversion: all inputs disabled, progress bar and log update
- Log: QTextEdit read-only, selectable/copyable, auto-scroll, timestamps
- Include collision: disabled with "Coming soon" tooltip
- On completion: success message, re-enable inputs
- On error: show in log (red text), re-enable inputs

## File Structure

```
ply2lcc/
├── CMakeLists.txt          # Add BUILD_GUI option
├── src/
│   ├── types.hpp           # Add ProgressCallback, update ConvertConfig
│   ├── convert_app.hpp/cpp # Add callback support
│   └── ...
└── gui/
    ├── CMakeLists.txt      # Qt6 build
    ├── main.cpp            # Qt entry point
    ├── mainwindow.hpp/cpp  # Main window
    └── convertworker.hpp/cpp # QThread worker
```

## Build

```bash
# CLI only (default)
cmake .. && make

# With GUI
cmake .. -DBUILD_GUI=ON && make
```

---

## Implementation Tasks

### Task 1: Add ProgressCallback and update ConvertConfig

**Files:**
- Modify: `src/types.hpp`

**Step 1:** Add ProgressCallback type alias after utility functions:
```cpp
#include <functional>

using ProgressCallback = std::function<void(int percent, const std::string& message)>;
```

**Step 2:** Update ConvertConfig struct:
```cpp
struct ConvertConfig {
    std::string input_path;    // PLY file or directory
    std::string output_dir;
    float cell_size_x = 30.0f;
    float cell_size_y = 30.0f;
    bool single_lod = false;
    bool include_env = true;   // NEW
    bool include_collision = false;  // NEW (placeholder)
};
```

**Step 3:** Commit
```bash
git add src/types.hpp
git commit -m "feat(types): add ProgressCallback and update ConvertConfig"
```

---

### Task 2: Modify ConvertApp constructor to accept ConvertConfig

**Files:**
- Modify: `src/convert_app.hpp`
- Modify: `src/convert_app.cpp`
- Modify: `src/main.cpp`

**Step 1:** Update convert_app.hpp - add new constructor and callback:
```cpp
class ConvertApp {
public:
    ConvertApp(int argc, char** argv);           // Keep for CLI
    ConvertApp(const ConvertConfig& config);     // NEW for GUI
    void setProgressCallback(ProgressCallback cb);
    void run();

private:
    ProgressCallback progress_cb_;
    void reportProgress(int percent, const std::string& msg);
    // ... rest unchanged
};
```

**Step 2:** Implement in convert_app.cpp:
```cpp
ConvertApp::ConvertApp(const ConvertConfig& config)
    : argc_(0), argv_(nullptr)
    , input_path_(config.input_path)
    , output_dir_(config.output_dir)
    , cell_size_x_(config.cell_size_x)
    , cell_size_y_(config.cell_size_y)
    , single_lod_(config.single_lod)
{
    // Derive input_dir_ and base_name_ from input_path_
    // ... (similar to parseArgs logic)
}

void ConvertApp::setProgressCallback(ProgressCallback cb) {
    progress_cb_ = std::move(cb);
}

void ConvertApp::reportProgress(int percent, const std::string& msg) {
    if (progress_cb_) {
        progress_cb_(percent, msg);
    }
}
```

**Step 3:** Commit
```bash
git add src/convert_app.hpp src/convert_app.cpp
git commit -m "feat(convert_app): add config constructor and progress callback"
```

---

### Task 3: Add progress reporting calls throughout ConvertApp::run()

**Files:**
- Modify: `src/convert_app.cpp`

**Step 1:** Add reportProgress calls at each stage:
```cpp
void ConvertApp::run() {
    reportProgress(0, "Starting conversion...");

    findPlyFiles();
    reportProgress(2, "Found " + std::to_string(lod_files_.size()) + " LOD files");

    validateOutput();
    reportProgress(5, "Building spatial grid...");

    buildSpatialGridParallel();
    reportProgress(15, "Encoding splats...");

    encodeAllLods();  // Add per-cell progress inside this method

    reportProgress(90, "Writing output files...");
    writeEncodedData();
    writeEnvironment();
    writeIndex();
    writeMeta();
    writeAttrs();

    reportProgress(100, "Conversion complete!");
}
```

**Step 2:** Add fine-grained progress in encodeAllLods():
```cpp
void ConvertApp::encodeAllLods() {
    const auto& cells = grid_->get_cells();
    size_t total_cells = cells.size();
    size_t processed = 0;

    for (const auto& [cell_id, cell] : cells) {
        // ... encoding logic ...

        processed++;
        int percent = 15 + (processed * 75 / total_cells);  // 15-90%
        reportProgress(percent, "Encoding cell " + std::to_string(processed) + "/" + std::to_string(total_cells));
    }
}
```

**Step 3:** Commit
```bash
git add src/convert_app.cpp
git commit -m "feat(convert_app): add progress reporting throughout conversion"
```

---

### Task 4: Create gui/ directory with Qt CMakeLists.txt

**Files:**
- Create: `gui/CMakeLists.txt`

**Step 1:** Create gui/CMakeLists.txt:
```cmake
cmake_minimum_required(VERSION 3.16)

find_package(Qt6 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(ply2lcc-gui
    main.cpp
    mainwindow.cpp
    mainwindow.hpp
    convertworker.cpp
    convertworker.hpp
)

target_link_libraries(ply2lcc-gui PRIVATE
    Qt6::Widgets
    ply2lcc_lib
)

target_include_directories(ply2lcc-gui PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
```

**Step 2:** Commit
```bash
git add gui/CMakeLists.txt
git commit -m "build(gui): add Qt6 CMakeLists.txt"
```

---

### Task 5: Implement ConvertWorker (QThread)

**Files:**
- Create: `gui/convertworker.hpp`
- Create: `gui/convertworker.cpp`

**Step 1:** Create convertworker.hpp:
```cpp
#ifndef CONVERTWORKER_HPP
#define CONVERTWORKER_HPP

#include <QThread>
#include <QString>
#include "types.hpp"

class ConvertWorker : public QThread {
    Q_OBJECT
public:
    explicit ConvertWorker(const ply2lcc::ConvertConfig& config, QObject* parent = nullptr);

signals:
    void progressChanged(int percent);
    void logMessage(const QString& message);
    void finished(bool success, const QString& error);

protected:
    void run() override;

private:
    ply2lcc::ConvertConfig config_;
};

#endif
```

**Step 2:** Create convertworker.cpp:
```cpp
#include "convertworker.hpp"
#include "convert_app.hpp"
#include <QDateTime>

ConvertWorker::ConvertWorker(const ply2lcc::ConvertConfig& config, QObject* parent)
    : QThread(parent), config_(config) {}

void ConvertWorker::run() {
    try {
        ply2lcc::ConvertApp app(config_);
        app.setProgressCallback([this](int percent, const std::string& msg) {
            emit progressChanged(percent);
            QString timestamp = QDateTime::currentDateTime().toString("[hh:mm:ss] ");
            emit logMessage(timestamp + QString::fromStdString(msg));
        });
        app.run();
        emit finished(true, QString());
    } catch (const std::exception& e) {
        emit finished(false, QString::fromStdString(e.what()));
    }
}
```

**Step 3:** Commit
```bash
git add gui/convertworker.hpp gui/convertworker.cpp
git commit -m "feat(gui): implement ConvertWorker thread"
```

---

### Task 6: Implement MainWindow

**Files:**
- Create: `gui/mainwindow.hpp`
- Create: `gui/mainwindow.cpp`

**Step 1:** Create mainwindow.hpp:
```cpp
#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QTextEdit>
#include <QProgressBar>
#include <QGroupBox>

class ConvertWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void browseInput();
    void browseOutput();
    void startConversion();
    void onProgressChanged(int percent);
    void onLogMessage(const QString& message);
    void onConversionFinished(bool success, const QString& error);

private:
    void setupUi();
    void setInputsEnabled(bool enabled);
    void updateConvertButton();

    QLineEdit* inputEdit_;
    QLineEdit* outputEdit_;
    QPushButton* inputBrowse_;
    QPushButton* outputBrowse_;
    QDoubleSpinBox* cellXSpin_;
    QDoubleSpinBox* cellYSpin_;
    QCheckBox* singleLodCheck_;
    QCheckBox* envCheck_;
    QCheckBox* collisionCheck_;
    QTextEdit* logEdit_;
    QProgressBar* progressBar_;
    QPushButton* convertBtn_;
    QGroupBox* settingsGroup_;

    ConvertWorker* worker_ = nullptr;
};

#endif
```

**Step 2:** Create mainwindow.cpp with full implementation (layout matches design).

**Step 3:** Commit
```bash
git add gui/mainwindow.hpp gui/mainwindow.cpp
git commit -m "feat(gui): implement MainWindow with full UI"
```

---

### Task 7: Create gui/main.cpp entry point

**Files:**
- Create: `gui/main.cpp`

**Step 1:** Create main.cpp:
```cpp
#include <QApplication>
#include "mainwindow.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ply2lcc Converter");

    MainWindow window;
    window.show();

    return app.exec();
}
```

**Step 2:** Commit
```bash
git add gui/main.cpp
git commit -m "feat(gui): add Qt application entry point"
```

---

### Task 8: Update root CMakeLists.txt with BUILD_GUI option

**Files:**
- Modify: `CMakeLists.txt`

**Step 1:** Add after the main executable definition:
```cmake
# GUI (optional)
option(BUILD_GUI "Build Qt GUI application" OFF)

if(BUILD_GUI)
    add_subdirectory(gui)
endif()
```

**Step 2:** Commit
```bash
git add CMakeLists.txt
git commit -m "build: add BUILD_GUI option for Qt GUI"
```

---

### Task 9: Test build on Linux

**Steps:**
1. Build CLI only: `cmake .. && make`
2. Build with GUI: `cmake .. -DBUILD_GUI=ON && make`
3. Run GUI: `./gui/ply2lcc-gui`
4. Test conversion with sample PLY file
5. Verify progress bar and log updates

**Commit:**
```bash
git commit --allow-empty -m "test: verify Qt GUI builds and runs on Linux"
```
