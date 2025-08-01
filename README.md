# Matroska Tags Editor

A simple desktop application for editing Matroska (MKV) file tags.

## ⚠️ Requirements

This application is a graphical front-end that relies on the **MKVToolnix** suite. Before using this application, **you must install MKVToolnix on your system** and ensure that its command-line tools (especially `mkvpropedit`) are available in your system's PATH.

## ✨ Features

*   **Core Functionality**: Reads, edits, and saves tags for Matroska files by invoking MKVToolnix tools.
*   **File Loading**: Supports loading files via a file dialog or by dragging and dropping them onto the window.
*   **User-Friendly Interface**: Provides common front-end conveniences like a tree view for tag structure and a context menu to simplify operations.

## 🛠️ Technology Stack

*   **Framework**: [Tauri](https://tauri.app/)
*   **Frontend**: [SvelteKit](https://kit.svelte.dev/) + [TypeScript](https://www.typescriptlang.org/)
*   **Backend**: [Rust](https://www.rust-lang.org/)

## 🚀 Getting Started

First, install the dependencies:

```bash
npm install
```

Then, to run the app in development mode:

```bash
npm run tauri dev
```

## 🙏 Acknowledgments

*   The file drag-and-drop feature was inspired by [tauri2-desktop-test](https://github.com/ZeronoFreya/tauri2-desktop-test).

## 💻 Recommended IDE Setup

*   [VS Code](https://code.visualstudio.com/)
*   [Svelte for VS Code](https://marketplace.visualstudio.com/items?itemName=svelte.svelte-vscode)
*   [Tauri](https://marketplace.visualstudio.com/items?itemName=tauri-apps.tauri-vscode)
*   [rust-analyzer](https://marketplace.visualstudio.com/items?itemName=rust-lang.rust-analyzer)
