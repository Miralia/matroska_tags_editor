<script lang="ts">
  /**
   * @component +page
   * This is the main page component of the application. It serves as the primary user interface,
   * handling file operations (opening, dropping, saving), displaying tag data,
   * and orchestrating user interactions with the data.
   */
  import { invoke } from "@tauri-apps/api/core";
  import { onMount } from "svelte";
  import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
  import TreeNode from "../lib/components/TreeNode.svelte";
  import Toolbar from "../lib/components/Toolbar.svelte";
  import ContextMenu from "../lib/components/ContextMenu.svelte";
  import {
    tagStore,
    type FileData,
    contextMenuStore,
  } from "../lib/store";
  import type { MenuItem } from "../lib/components/ContextMenu.svelte";
  import { listen } from "@tauri-apps/api/event";
  import { WebviewWindow } from "@tauri-apps/api/webviewWindow";
  import { useGlobalDrag } from '../lib/useGlobalDrag';

  const { dragStatus } = useGlobalDrag();

  /** The full, absolute path of the currently loaded file. */
  let fullPath = $state("");
  /** The potentially truncated path displayed in the input field to fit the UI. */
  let displayPath = $state("");
  /** Stores any error message that occurs during file operations. */
  let error = $state("");
  /** A reactive reference to the file path input element. */
  let inputElement: HTMLInputElement | null = $state(null);
  /** A 2D rendering context used for measuring text width to truncate the display path. */
  let ctx: CanvasRenderingContext2D | null = $state(null);
  /** A boolean state to track if the file path input is focused, to show the full path. */
  let isPathFocused = $state(false);

  /**
   * Handles the right-click event on the main content area to show a context menu.
   * @param {MouseEvent} event - The mouse event.
   */
  function handleContextMenu(event: MouseEvent) {
    event.preventDefault();

    const contextMenuItems: MenuItem[] = [
      {
        label: "Undo",
        action: tagStore.undo,
        disabled: $tagStore.historyIndex <= 0,
      },
      {
        label: "Redo",
        action: tagStore.redo,
        disabled: $tagStore.historyIndex >= $tagStore.history.length - 1,
      },
      { separator: true },
      {
        label: "Add Tag",
        action: tagStore.addTag,
        disabled: !$tagStore.selectedContext,
      },
      {
        label: "Paste",
        action: tagStore.pasteTags,
        disabled: $tagStore.clipboard.length === 0,
      },
      { separator: true },
      { label: "Select All", action: tagStore.selectAllTags },
    ];

    contextMenuStore.open(event.clientX, event.clientY, contextMenuItems);
  }

  /**
   * Loads a Matroska file by invoking the backend, parsing the data,
   * and updating the store.
   * @param {string} path - The absolute path of the file to load.
   */
  async function loadFile(path: string) {
    error = "";
    try {
      fullPath = path;
      const data = await invoke<FileData>("get_file_data", { path });
      tagStore.loadFile(data, fullPath);
    } catch (e) {
      error = e as string;
    }
  }

  /**
   * Handles the file drop event. It checks for unsaved changes,
   * then invokes the backend to get the dropped file paths and loads the first file.
   */
  async function handleFileDrop() {
    console.log(`[+page.svelte] handleFileDrop triggered. Current drag status: ${$dragStatus}`);
    if ($dragStatus !== 'external') return;

    try {
      let currentState: any;
      const unsub = tagStore.subscribe((s) => {
        currentState = s;
      });
      unsub();

      if (currentState.isDirty) {
        const { ask } = await import("@tauri-apps/plugin-dialog");
        const confirmed = await ask(
          "You have unsaved changes. Are you sure you want to open a new file and discard them?",
          {
            title: "Unsaved Changes",
            kind: "warning",
          },
        );
        if (!confirmed) {
          return;
        }
      }

      const files = await invoke<{ path: string }[]>("take_drop_files");
      console.log('[+page.svelte] Files received from backend:', files);
      if (files && files.length > 0) {
        // Handle only the first file for simplicity
        console.log(`[+page.svelte] Loading file: ${files[0].path}`);
        await loadFile(files[0].path);
      }
    } catch (e) {
      console.error("Error handling file drop:", e);
      error = e as string;
    }
  }

  /**
   * Opens a file dialog to allow the user to select a file.
   * It checks for unsaved changes before proceeding.
   */
  async function openFile() {
    try {
      let currentState: any;
      const unsub = tagStore.subscribe((s) => {
        currentState = s;
      });
      unsub();

      if (currentState.isDirty) {
        const { ask } = await import("@tauri-apps/plugin-dialog");
        const confirmed = await ask(
          "You have unsaved changes. Are you sure you want to open a new file and discard them?",
          {
            title: "Unsaved Changes",
            kind: "warning",
          },
        );
        if (!confirmed) {
          return;
        }
      }

      const result = await invoke("open_file_dialog");
      if (typeof result === "string") {
        await loadFile(result);
      }
    } catch (e) {
      error = e as string;
    }
  }

  /**
   * Handles keyboard events on the welcome screen to trigger file open.
   * @param {KeyboardEvent} event - The keyboard event.
   */
  function handleWelcomeKeydown(event: KeyboardEvent) {
    if (event.key === "Enter" || event.key === " ") {
      openFile();
    }
  }

  /**
   * onMount lifecycle hook.
   * Sets up global event listeners for keyboard shortcuts, drag-and-drop prevention,
   * and handling the window's close request to check for unsaved changes.
   */
  onMount(() => {
    const appWindow = getCurrentWebviewWindow();

    invoke("create_drop_window");
 
    const unsub = appWindow.listen("close-requested", async () => {
      let currentState: any;
      const unsubStore = tagStore.subscribe((s) => {
        currentState = s;
      });
      unsubStore();

      if (!currentState.isDirty) {
        await invoke("force_close");
        return;
      }

      const { ask } = await import("@tauri-apps/plugin-dialog");
      const confirmed = await ask(
        "You have unsaved changes. Are you sure you want to close the application?",
        {
          title: "Unsaved Changes",
          kind: "warning",
        },
      );

      if (confirmed) {
        await invoke("force_close");
      }
    });


    const canvas = document.createElement("canvas");
    ctx = canvas.getContext("2d");

    const handleGlobalKeyDown = (event: KeyboardEvent) => {
      const isMac = navigator.platform.toUpperCase().indexOf("MAC") >= 0;
      const modifier = isMac ? event.metaKey : event.ctrlKey;

      if (modifier) {
        switch (event.key.toLowerCase()) {
          case "c":
            tagStore.copySelectedTags();
            break;
          case "x":
            tagStore.cutSelectedTags();
            break;
          case "v":
            tagStore.pasteTags();
            break;
          case "z":
            if (event.shiftKey) {
              tagStore.redo();
            } else {
              tagStore.undo();
            }
            break;
          case "a":
            event.preventDefault();
            tagStore.selectAllTags();
            break;
          case "s":
            event.preventDefault();
            tagStore.save();
            break;
        }
      } else if (event.key === "Delete" || event.key === "Backspace") {
        tagStore.deleteSelectedTags();
      }
    };

    window.addEventListener("keydown", handleGlobalKeyDown);

    const preventDefaultDrag = (event: DragEvent) => {
      event.preventDefault();
    };

    window.addEventListener("dragover", preventDefaultDrag);
    window.addEventListener("drop", preventDefaultDrag);

    return () => {
      window.removeEventListener("keydown", handleGlobalKeyDown);
      window.removeEventListener("dragover", preventDefaultDrag);
      window.removeEventListener("drop", preventDefaultDrag);
      // The promise resolves to a function to unsubscribe
      unsub.then((fn) => fn());
    };
  });

  /**
   * A reactive effect that runs whenever the full path or input element dimensions change.
   * It calculates a shortened `displayPath` to ensure the file path fits within the input field
   * without overflowing, adding an ellipsis (...) to the directory part if necessary.
   */
  $effect(() => {
    if (!fullPath || !inputElement || !ctx) {
      displayPath = fullPath;
      return;
    }

    const style = getComputedStyle(inputElement);
    ctx.font = `${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;

    const inputWidth =
      inputElement.clientWidth -
      (parseFloat(style.paddingLeft) + parseFloat(style.paddingRight));
    const fullPathWidth = ctx.measureText(fullPath).width;

    if (fullPathWidth <= inputWidth) {
      displayPath = fullPath;
      return;
    }

    const separator = fullPath.includes("/") ? "/" : "\\";
    const lastSeparatorIndex = fullPath.lastIndexOf(separator);

    if (lastSeparatorIndex === -1) {
      displayPath = fullPath;
      return;
    }

    const filename = fullPath.substring(lastSeparatorIndex + 1);
    let directory = fullPath.substring(0, lastSeparatorIndex);
    const ellipsis = "...";
    const endPart = `${separator}${filename}`;

    while (directory.length > 0) {
      const tempPath = `${directory}${ellipsis}${endPart}`;
      if (ctx.measureText(tempPath).width <= inputWidth) {
        displayPath = tempPath;
        return;
      }
      directory = directory.slice(0, -1);
    }

    displayPath = `${ellipsis}${endPart}`;
  });
</script>

<main class="container">
  {#if !fullPath}
    <div
      class="welcome-container"
      role="button"
      tabindex="0"
      onclick={openFile}
      onkeydown={handleWelcomeKeydown}
      ondrop={handleFileDrop}
    >
      <div
        class="welcome-indicator"
      >
        <svg
          width="80"
          height="80"
          viewBox="0 0 24 24"
          fill="none"
          xmlns="http://www.w3.org/2000/svg"
        >
          <path
            d="M12 4V20M4 12H20"
            stroke="#888"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
          />
        </svg>
      </div>
      <p class="welcome-text">Open a Matroska File</p>
    </div>
  {:else}
    <div
      class="main-content"
      role="button"
      tabindex="0"
      onclick={(e) => {
        if (e.target === e.currentTarget) {
          tagStore.clearSelections();
        }
      }}
      onkeydown={(e) => {
        if (
          e.target === e.currentTarget &&
          (e.key === "Enter" || e.key === " ")
        ) {
          tagStore.clearSelections();
        }
      }}
      ondrop={handleFileDrop}
    >
      <div class="row">
        <input
          type="text"
          readonly
          title={fullPath}
          value={isPathFocused ? fullPath : displayPath}
          onfocus={() => (isPathFocused = true)}
          onblur={() => (isPathFocused = false)}
          bind:this={inputElement}
          class="file-path-input"
        />
        <button
          onclick={openFile}
        >
          Open
        </button>
      </div>

      {#if error}
        <div class="error">
          <p>An error occurred:</p>
          <pre>{error}</pre>
        </div>
      {/if}

      {#if $tagStore.fileData && ($tagStore.fileData.global_tags.length > 0 || $tagStore.fileData.tracks.length > 0)}
        <div class="data-display">
          <div class="table-header">
            <div class="col-name">Tag Name</div>
            <div class="col-value">Tag Value</div>
          </div>
          <div
            class="table-body"
            role="tree"
            tabindex="0"
            oncontextmenu={handleContextMenu}
          >
            {#if $tagStore.fileData.global_tags}
              <TreeNode
                title="Global"
                tags={$tagStore.fileData.global_tags}
                trackId="global"
              />
            {/if}
            {#if $tagStore.fileData.tracks && $tagStore.fileData.tracks.length > 0}
              {#each $tagStore.fileData.tracks as track (track.id)}
                <TreeNode
                  title={`${track.display_name}${track.track_name ? ` (${track.track_name})` : ""}`}
                  tags={track.tags}
                  trackId={track.id!}
                />
              {/each}
            {/if}
          </div>
        </div>
      {/if}
    </div>
    <Toolbar />
  {/if}
</main>

<ContextMenu />


<style>
  .container {
    margin: 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    text-align: center;
    height: 100vh;
  }

  .welcome-container {
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    cursor: pointer;
    height: 100%;
  }

  .welcome-indicator {
    width: 150px;
    height: 150px;
    border: 2px dashed #888;
    border-radius: 16px;
    display: flex;
    justify-content: center;
    align-items: center;
    margin-bottom: 1rem;
    transition:
      background-color 0.2s,
      border-color 0.2s;
  }

  .welcome-container:hover .welcome-indicator {
    background-color: rgba(136, 136, 136, 0.1);
    border-color: #fff;
  }

  .welcome-text {
    color: #888;
    font-size: 1.2em;
    transition: color 0.2s;
  }

  .main-content {
    padding-top: 2vh;
    height: 100%;
    display: flex;
    flex-direction: column;
    flex-grow: 1;
    min-height: 0;
  }

  .file-path-input {
    width: 300px;
    margin-right: 10px;
  }

  .row {
    display: flex;
    justify-content: center;
    margin-bottom: 1rem;
    flex-shrink: 0;
  }

  .data-display {
    text-align: left;
    background-color: #f0f0f0;
    border-radius: 8px;
    overflow-y: auto;
    margin-top: 1rem;
    flex-grow: 1;
    min-height: 0;
    display: flex;
    flex-direction: column;
    font-family: "SF Mono", "Consolas", "Menlo", "Courier New", monospace;
  }

  .table-header {
    display: flex;
    font-weight: bold;
    border-bottom: 2px solid #ccc;
    padding: 0.5rem 1rem;
    background-color: #e9e9e9;
    flex-shrink: 0;
  }

  .table-body {
    padding: 0 1rem;
    overflow-y: auto;
    flex-grow: 1;
  }

  .table-header .col-name {
    padding-left: 21px;
  }

  .col-name,
  .col-value {
    box-sizing: border-box;
  }

  .col-name {
    width: 35%;
  }

  .col-value {
    width: 65%;
  }

  .error {
    color: red;
    text-align: left;
    background-color: #ffe0e0;
    padding: 1rem;
    border-radius: 8px;
    margin-top: 1rem;
  }

  pre {
    white-space: pre-wrap;
    word-wrap: break-word;
  }

  @media (prefers-color-scheme: dark) {
    .data-display {
      background-color: #3a3a3a;
    }

    .table-header {
      background-color: #3c3c3c;
      border-bottom-color: #5a5a5a;
      color: #f0f0f0;
    }

    .error {
      background-color: #5a2020;
    }

    .welcome-indicator {
      border-color: #666;
    }

    .welcome-container:hover .welcome-indicator {
      background-color: rgba(102, 102, 102, 0.2);
    }

    .welcome-text {
      color: #888;
    }
  }
</style>
