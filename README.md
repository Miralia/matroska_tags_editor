# Matroska Tags Editor

**A minimalist, cross-platform desktop application for editing Matroska (MKV, MKA, MK3D) file tags.**

This application provides a clean and simple graphical user interface for the powerful command-line tools from the [MKVToolnix](https://mkvtoolnix.download/) suite. It allows you to effortlessly manage tags in your media files without needing to master the command line.

---

## Project Overview

The Matroska Tags Editor is designed for users who want a straightforward way to read, edit, and save metadata tags in Matroska container files. Whether you're organizing your media library or correcting information in a video file, this tool streamlines the process by acting as a user-friendly front-end for `mkvpropedit`. It supports loading files through a standard dialog or by simply dragging and dropping them into the application window.

## Key Features

- **Cross-Platform**: Runs on Windows, macOS, and Linux.
- **Simple Tag Management**: Easily view, edit, and update tag information in a tree-like structure.
- **Direct File Interaction**: Load files via a file-picker or intuitive drag-and-drop functionality.
- **Powered by MKVToolnix**: Leverages the robust and reliable MKVToolnix backend for all file operations.
- **Modern UI**: Built with a clean and responsive interface for a seamless user experience.

## Installation

You can download the latest compiled version for your operating system directly from the **[GitHub Releases](https://github.com/Miralia/matroska_tags_editor/releases)** page.

## Crucial Prerequisite

This application **requires MKVToolnix to be installed on your system**.

It is a graphical front-end and does not include the core MKV editing tools. Please download and install the appropriate package for your OS from the [official MKVToolnix website](https://mkvtoolnix.download/downloads.html) and ensure the command-line tools (e.g., `mkvpropedit`) are accessible via your system's PATH environment variable.

## For Developers

This project is built using Tauri, SvelteKit, and Rust.

### Development Setup

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/Miralia/matroska_tags_editor.git
    cd matroska_tags_editor
    ```

2.  **Install Node.js dependencies:**
    ```bash
    npm install
    ```

3.  **Run the development environment:**
    ```bash
    npm run tauri dev
    ```

## License

This project is licensed under the MIT License. See the `LICENSE` file for more details.