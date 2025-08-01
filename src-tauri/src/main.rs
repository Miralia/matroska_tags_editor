//! Prevents an additional console window from appearing on Windows in release builds.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

/// The main entry point of the application.
fn main() {
    // Calls the `run` function from the `matroska_tags_editor_lib` crate to start the Tauri application.
    matroska_tags_editor_lib::run()
}
