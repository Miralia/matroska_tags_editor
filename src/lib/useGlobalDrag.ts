/**
 * @module useGlobalDrag
 * A Svelte hook to manage global drag-and-drop state, distinguishing between
 * internal (within the app) and external (files dragged from the OS) drag events.
 */
import { writable, get } from 'svelte/store';
import { invoke } from '@tauri-apps/api/core';
import { onMount } from 'svelte';

/**
 * Enum for the different states of a drag operation.
 * - `IDLE`: No drag operation is in progress.
 * - `INTERNAL_DRAG`: An element from within the application is being dragged.
 * - `EXTERNAL_OVER`: An external file is being dragged over the application window.
 */
enum DragState {
  IDLE = 'idle',
  INTERNAL_DRAG = 'internal',
  EXTERNAL_OVER = 'external',
}

/** A writable Svelte store that holds the current `DragState`. */
const dragStatus = writable<DragState>(DragState.IDLE);
/** A flag to ensure that global event listeners are only bound once. */
let eventBound = false;

/**
 * A custom Svelte hook that sets up global listeners to track drag-and-drop state.
 * This is essential for showing a drop zone only when external files are dragged over the window.
 * @returns An object containing the reactive `dragStatus` store.
 */
export function useGlobalDrag() {
  const setupListeners = () => {
    if (eventBound) return;

    /** Handles the start of any drag event, setting the state to internal. */
    const handleDragStart = () => {
      console.log('[useGlobalDrag] Drag Start detected. Setting state to INTERNAL_DRAG.');
      dragStatus.set(DragState.INTERNAL_DRAG);
    };

    /** Handles a drag enter event. If idle, it means an external file is being dragged over. */
    const handleDragEnter = (event: DragEvent) => {
      event.preventDefault();
      if (get(dragStatus) === DragState.IDLE) {
        console.log('[useGlobalDrag] Drag Enter detected from external. Setting state to EXTERNAL_OVER.');
        dragStatus.set(DragState.EXTERNAL_OVER);
        invoke('show_drop_window');
      }
    };

    /** Handles the end of a drag operation (drop or cancel), resetting the state. */
    const handleDragEnd = () => {
      console.log('[useGlobalDrag] Drag End detected. Resetting state to IDLE and hiding drop window.');
      // Use a timeout to prevent flickering when dragging from one internal element to another.
      setTimeout(() => {
        dragStatus.set(DragState.IDLE);
      }, 100);
      invoke('hide_drop_window');
    };

    document.addEventListener('dragstart', handleDragStart, { capture: true });
    document.addEventListener('dragenter', handleDragEnter, { capture: true });
    document.addEventListener('drop', handleDragEnd, { capture: true });
    document.addEventListener('dragend', handleDragEnd, { capture: true });

    eventBound = true;
  };

  onMount(() => {
    setupListeners();
  });

  return {
    dragStatus,
  };
}