use serde::{Deserialize, Serialize};
use std::sync::{LazyLock, Mutex};
use tauri::Manager;

/// Represents information about a dropped file or directory.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct FileInfo {
    /// The path to the file or directory.
    pub path: String,
    /// Whether this path represents a directory.
    pub is_dir: bool,
}

/// A global, thread-safe container for storing information about files
/// that have been dropped onto the drop window.
static DROP_FILES: LazyLock<Mutex<Vec<FileInfo>>> = LazyLock::new(|| Mutex::new(Vec::new()));

/// Creates a dedicated, transparent window for handling file drops.
/// This window overlays the main window when an external drag is detected.
#[tauri::command]
pub async fn create_drop_window(app: tauri::AppHandle) -> Result<(), String> {
    log::info!("[drop.rs] create_drop_window called");
    if app.get_webview_window("drop-window").is_some() {
        log::info!("[drop.rs] drop-window already exists.");
        return Ok(());
    }

    let drop_window = tauri::WebviewWindowBuilder::new(
        &app,
        "drop-window",
        tauri::WebviewUrl::App("about:blank".into()),
    )
    .title("Drop Window")
    .inner_size(400.0, 300.0)
    .decorations(false)
    .shadow(false)
    .always_on_top(true)
    .transparent(true)
    .skip_taskbar(true)
    .visible(false)
    .build()
    .map_err(|e| format!("Failed to create window: {}", e))?;
    setup_drop_window_events(&drop_window, app.clone());
    Ok(())
}

use tauri::{DragDropEvent, WindowEvent};

/// Sets up the event listeners for the drop window.
/// This function listens for the `DragDropEvent::Enter` event to capture dropped file paths.
fn setup_drop_window_events(window: &tauri::WebviewWindow, app: tauri::AppHandle) {
    window.on_window_event(move |event| {
        if let WindowEvent::DragDrop(drag_event) = event {
            match drag_event {
                DragDropEvent::Enter { paths, position: _ } => {
                    let mut file_infos = Vec::new();
                    for path in paths {
                        let is_dir = path.is_dir();
                        file_infos.push(FileInfo {
                            path: path.to_string_lossy().into_owned(),
                            is_dir,
                        });
                    }
                    if let Ok(mut drop_files) = DROP_FILES.lock() {
                        *drop_files = file_infos;
                        log::debug!("[drop.rs] Stored files in DROP_FILES");
                        drop(drop_files); // Release the lock immediately
                        app.get_webview_window("drop-window")
                            .and_then(|w| {
                                log::info!("[drop.rs] Hiding drop-window after storing files.");
                                w.hide().ok()
                            });
                    }
                }
                _ => {}
            }
        }
    });
}

/// Retrieves the list of dropped files and clears the global container.
/// This is a "take" operation, meaning the files are consumed and will not be available on subsequent calls.
#[tauri::command]
pub fn take_drop_files() -> Result<Vec<FileInfo>, String> {
    log::info!("[drop.rs] take_drop_files called");
    let mut drop_files = DROP_FILES.lock().map_err(|e| e.to_string())?;
    let files = std::mem::take(&mut *drop_files);
    log::debug!("[drop.rs] Returning files: {:?}", files);
    Ok(files)
}

/// Hides the drop window.
#[tauri::command]
pub async fn hide_drop_window(app: tauri::AppHandle) -> Result<(), String> {
    log::info!("[drop.rs] hide_drop_window called");
    if let Some(window) = app.get_webview_window("drop-window") {
        window.hide().map_err(|e| e.to_string())?;
    }
    Ok(())
}

/// Shows the drop window and positions it to overlay the main window.
#[tauri::command]
pub async fn show_drop_window(app: tauri::AppHandle) -> Result<(), String> {
    if let Some(window) = app.get_webview_window("drop-window") {
        if let Some(main_window) = app.get_webview_window("main") {
            let position = main_window.outer_position().map_err(|e| e.to_string())?;
            let size = main_window.outer_size().map_err(|e| e.to_string())?;

            window.set_position(position).map_err(|e| e.to_string())?;
            window.set_size(size).map_err(|e| e.to_string())?;
        }
        window.show().map_err(|e| e.to_string())?;
    }
    Ok(())
}