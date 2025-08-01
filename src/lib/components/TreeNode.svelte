<script lang="ts">
  /**
   * @component TreeNode
   * A recursive component that displays a collapsible node in the tag tree.
   * It represents either the "Global" tags or a specific track's tags.
   * It handles user interactions like selection, editing, context menus, and drag-and-drop.
   */
  import { tagStore, type Tag, contextMenuStore } from '../store';
  import type { MenuItem } from './ContextMenu.svelte';
  import { useGlobalDrag } from '../useGlobalDrag';

  const { dragStatus } = useGlobalDrag();

  const {
    /** The title displayed in the node header (e.g., "Global" or "Video #1"). */
    title,
    /** The array of tags to be displayed within this node. */
    tags,
    /** The unique identifier for this node's context ("global" or a track ID). */
    trackId
  } = $props<{ title: string; tags: Tag[]; trackId: string; }>();

  /** Whether the node's content is currently expanded or collapsed. */
  let isOpen = $state(true);
  /** The ID of the tag currently being edited, or null if no tag is being edited. */
  let editingTagId: string | null = $state(null);
  /** The specific field ('Name' or 'String') of the tag currently being edited. */
  let editingField: 'Name' | 'String' | null = $state(null);
  /** The current value in the input field while editing a tag. */
  let editingValue = $state('');
  /** The ID of the tag or track that the user is currently dragging over. */
  let dragOverTargetId: string | null = $state(null);
  /** The calculated drop position ('before', 'after', or 'append') relative to the drag-over target. */
  let dropPosition: 'before' | 'after' | 'append' | null = $state(null);

  /**
   * Handles the right-click event on an individual tag row.
   * It ensures the tag is selected and then opens the context menu with relevant actions.
   * @param {MouseEvent} event - The mouse event.
   * @param {Tag} tag - The tag that was right-clicked.
   */
  function handleContextMenu(event: MouseEvent, tag: Tag) {
    event.preventDefault();
    event.stopPropagation();

    if (!$tagStore.selectedTagIds.has(tag.id!)) {
      tagStore.toggleSelection(tag.id!, new MouseEvent('click'));
    }

    const contextMenuItems: MenuItem[] = [
      { label: 'Undo', action: tagStore.undo, disabled: $tagStore.historyIndex <= 0 },
      { label: 'Redo', action: tagStore.redo, disabled: $tagStore.historyIndex >= $tagStore.history.length - 1 },
      { separator: true },
      { label: 'Cut', action: tagStore.cutSelectedTags, disabled: $tagStore.selectedTagIds.size === 0 },
      { label: 'Copy', action: tagStore.copySelectedTags, disabled: $tagStore.selectedTagIds.size === 0 },
      { label: 'Paste', action: tagStore.pasteTags, disabled: $tagStore.clipboard.length === 0 },
      { separator: true },
      { label: 'Delete', action: tagStore.deleteSelectedTags, disabled: $tagStore.selectedTagIds.size === 0 },
      { separator: true },
      { label: 'Select All', action: tagStore.selectAllTags },
    ];

    contextMenuStore.open(event.clientX, event.clientY, contextMenuItems);
  }

  /**
   * Handles the right-click event on the node's header (e.g., "Global" or "Video #1").
   * It sets the current context and opens a context menu with actions applicable to the entire node.
   * @param {MouseEvent} event - The mouse event.
   */
  function handleHeaderContextMenu(event: MouseEvent) {
    event.preventDefault();
    event.stopPropagation();

    tagStore.setSelectedContext(trackId);
    $tagStore.selectedTagIds.clear();

    const nodeHasTags = tags.length > 0;

    const contextMenuItems: MenuItem[] = [
      { label: 'Add Tag', action: tagStore.addTag },
      { separator: true },
      { label: 'Copy All Tags', action: tagStore.copySelectedTags, disabled: !nodeHasTags },
      { label: 'Cut All Tags', action: tagStore.cutSelectedTags, disabled: !nodeHasTags },
      { label: 'Paste Tags', action: tagStore.pasteTags, disabled: $tagStore.clipboard.length === 0 },
      { separator: true },
      { label: 'Delete All Tags', action: tagStore.deleteSelectedTags, disabled: !nodeHasTags },
    ];

    contextMenuStore.open(event.clientX, event.clientY, contextMenuItems);
  }

  /**
   * A reactive effect that triggers when a new tag is added to the store.
   * If the new tag belongs to this TreeNode, it automatically enters editing mode for the tag's name.
   */
  $effect(() => {
    const newlyAddedTagId = $tagStore.newlyAddedTagId;
    if (newlyAddedTagId) {
      const newTag = tags.find((t: Tag) => t.id === newlyAddedTagId);
      if (newTag) {
        startEditing(newTag, 'Name');
        tagStore.clearNewlyAddedTagId();
      }
    }
  });

  /** Toggles the collapsed/expanded state of the tree node. */
  function toggle() {
    isOpen = !isOpen;
  }

  /**
   * Initiates the editing mode for a specific field of a tag.
   * @param {Tag} tag - The tag to be edited.
   * @param {'Name' | 'String'} field - The field of the tag to edit.
   */
  function startEditing(tag: Tag, field: 'Name' | 'String') {
    editingTagId = tag.id!;
    editingField = field;
    editingValue = tag[field];
  }

  /**
   * Commits the edited value to the store and exits editing mode.
   * It trims the value and only updates if the value has actually changed.
   * @param {Tag} tag - The tag that was being edited.
   */
  function handleEdit(tag: Tag) {
    if (editingTagId === null || editingField === null) return;

    const trimmedValue = editingValue.trim();
    if (trimmedValue !== tag[editingField]) {
      tagStore.updateTag(trackId, editingTagId, { [editingField]: trimmedValue });
    }

    editingTagId = null;
    editingField = null;
  }

  /**
   * Handles keyboard events within the tag editing input field.
   * Commits the edit on 'Enter' and cancels it on 'Escape'.
   * @param {KeyboardEvent} event - The keyboard event.
   * @param {Tag} tag - The tag being edited.
   */
  function handleKeyDown(event: KeyboardEvent, tag: Tag) {
    event.stopPropagation();
    if (event.key === 'Enter') {
      handleEdit(tag);
    } else if (event.key === 'Escape') {
      editingTagId = null;
      editingField = null;
    }
  }

  /**
   * Handles the start of a drag operation on a tag.
   * @param {DragEvent} event - The drag event.
   * @param {string} tagId - The ID of the tag being dragged.
   */
  function handleDragStart(event: DragEvent, tagId: string) {
    event.dataTransfer!.setData('text/plain', tagId);
  }

  /** Resets the drag-over state when a drag operation ends. */
  function handleDragEnd() {
    dragOverTargetId = null;
    dropPosition = null;
  }

  /**
   * Handles the drop event, moving the dragged tag to the new position.
   * @param {DragEvent} event - The drop event.
   */
  function handleDrop(event: DragEvent) {
    if ($dragStatus !== 'internal') {
      dragOverTargetId = null;
      dropPosition = null;
      return;
    }
    event.preventDefault();
    if (dragOverTargetId && dropPosition) {
      const draggedId = event.dataTransfer!.getData('text/plain');
      if (draggedId !== dragOverTargetId) {
        tagStore.moveTag(draggedId, dragOverTargetId, dropPosition);
      }
    }
    dragOverTargetId = null;
    dropPosition = null;
  }

  /**
   * Handles the drag-over event on a tag row, determining the drop position (before/after).
   * @param {DragEvent} event - The drag event.
   * @param {string} targetTagId - The ID of the tag being dragged over.
   */
  function handleDragOver(event: DragEvent, targetTagId: string) {
    event.preventDefault();
    const targetElement = event.currentTarget as HTMLElement;
    const rect = targetElement.getBoundingClientRect();
    const isOverTopHalf = event.clientY < rect.top + rect.height / 2;

    dragOverTargetId = targetTagId;
    dropPosition = isOverTopHalf ? 'before' : 'after';
  }

  /** Resets the drag-over state when the dragged item leaves a potential drop target. */
  function handleDragLeave() {
    dragOverTargetId = null;
    dropPosition = null;
  }

  /**
   * Handles the drag-over event on the node header, setting the drop position to 'append'.
   * @param {DragEvent} event - The drag event.
   */
  function handleHeaderDragOver(event: DragEvent) {
    event.preventDefault();
    dragOverTargetId = trackId;
    dropPosition = 'append';
  }

  /**
   * A Svelte action to automatically focus an element when it is mounted to the DOM.
   * @param {HTMLElement} node - The DOM node to focus.
   */
  function autofocus(node: HTMLElement) {
    node.focus();
  }

  /**
   * Handles keydown events on a tag row for accessibility, allowing selection with Enter or Space.
   * @param {KeyboardEvent} event - The keyboard event.
   * @param {string} tagId - The ID of the tag in the row.
   */
  function handleRowKeyDown(event: KeyboardEvent, tagId: string) {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      const mockMouseEvent = new MouseEvent('click', {
        ctrlKey: event.ctrlKey,
        metaKey: event.metaKey,
        shiftKey: event.shiftKey,
      });
      tagStore.toggleSelection(tagId, mockMouseEvent);
    }
  }

  /**
   * Handles keydown events on a tag cell (Name or String), allowing editing to be initiated with Enter or Space.
   * @param {KeyboardEvent} event - The keyboard event.
   * @param {Tag} tag - The tag associated with the cell.
   * @param {'Name' | 'String'} field - The field associated with the cell.
   */
  function handleCellKeyDown(event: KeyboardEvent, tag: Tag, field: 'Name' | 'String') {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      startEditing(tag, field);
    }
  }

  /**
   * Handles keydown events on the node header, allowing context selection with Enter or Space.
   * @param {KeyboardEvent} event - The keyboard event.
   */
  function handleHeaderKeyDown(event: KeyboardEvent) {
    if (event.key === 'Enter' || event.key === ' ') {
      event.preventDefault();
      tagStore.setSelectedContext(trackId);
    }
  }
</script>

<div class="tree-node">
  <div
    role="button"
    tabindex="0"
    class="node-header"
    class:selected={$tagStore.selectedContext === trackId && $tagStore.selectedTagIds.size === 0}
    class:drag-over-append={dragOverTargetId === trackId && dropPosition === 'append'}
    onclick={() => {
      tagStore.setSelectedContext(trackId);
      $tagStore.selectedTagIds.clear();
    }}
    onkeydown={handleHeaderKeyDown}
    ondragover={handleHeaderDragOver}
    ondragleave={handleDragLeave}
    ondrop={handleDrop}
    oncontextmenu={handleHeaderContextMenu}
  >
    <button
      type="button"
      class="toggle-icon"
      onclick={(event) => { event.stopPropagation(); toggle(); }}
      aria-label={isOpen ? 'Collapse' : 'Expand'}
    >{isOpen ? '−' : '+'}</button>
    <div class="col-name"><strong>{title}</strong></div>
  </div>

  {#if isOpen && tags}
    <div class="node-content">
      {#each tags as tag (tag.id)}
        <div
          class="tag-row"
          class:selected={$tagStore.selectedTagIds.has(tag.id!)}
          class:drag-over-before={dragOverTargetId === tag.id && dropPosition === 'before'}
          class:drag-over-after={dragOverTargetId === tag.id && dropPosition === 'after'}
          role="button"
          tabindex="0"
          onclick={(e) => tagStore.toggleSelection(tag.id!, e)}
          onkeydown={(e) => handleRowKeyDown(e, tag.id!)}
          draggable="true"
          ondragstart={(e) => handleDragStart(e, tag.id!)}
          ondragend={handleDragEnd}
          ondrop={handleDrop}
          ondragover={(e) => handleDragOver(e, tag.id!)}
          ondragleave={handleDragLeave}
        oncontextmenu={(e) => handleContextMenu(e, tag)}
        >
                  <div
                    class="col-name"
                    role="button"
            tabindex="0"
            ondblclick={() => startEditing(tag, 'Name')}
            onkeydown={(e) => handleCellKeyDown(e, tag, 'Name')}
          >
            {#if editingTagId === tag.id && editingField === 'Name'}
              <input
                type="text"
                placeholder="Enter Name"
                bind:value={editingValue}
                onblur={() => handleEdit(tag)}
                onkeydown={(e) => handleKeyDown(e, tag)}
                use:autofocus
              />
            {:else}
              {#if tag.Name}
                {tag.Name}
              {:else}
                <span class="placeholder">(Click to edit)</span>
              {/if}
            {/if}
          </div>
          <div
            class="col-value"
            role="button"
            tabindex="0"
            ondblclick={() => startEditing(tag, 'String')}
            onkeydown={(e) => handleCellKeyDown(e, tag, 'String')}
          >
            {#if editingTagId === tag.id && editingField === 'String'}
              <input
                type="text"
                placeholder="Enter Value"
                bind:value={editingValue}
                onblur={() => handleEdit(tag)}
                onkeydown={(e) => handleKeyDown(e, tag)}
                use:autofocus
              />
            {:else}
              {#if tag.String}
                {tag.String}
              {:else}
                <span class="placeholder">(Click to edit)</span>
              {/if}
            {/if}
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>

<style>
  .tree-node {
    padding: 0 4px;
  }
  .node-header {
    display: flex;
    align-items: center;
    cursor: pointer;
    padding: 5px 8px;
    user-select: none;
    background: none;
    border: none;
    text-align: left;
    font-family: inherit;
    font-size: inherit;
    color: inherit;
    border-radius: 4px;
  }

  .node-header:hover, .node-header.selected {
    background-color: #e0e0e0;
  }

  @media (prefers-color-scheme: dark) {
    .node-header:hover, .node-header.selected {
      background-color: #4a4a4a;
    }
  }

  .toggle-icon {
    flex-shrink: 0;
    width: 16px;
    height: 16px;
    margin-right: 5px;
    text-align: center;
    cursor: pointer;
    background: #e0e0e0;
    border: none;
    border-radius: 4px;
    padding: 0;
    font-size: 12px;
    font-weight: bold;
    color: inherit;
    display: flex;
    align-items: center;
    justify-content: center;
    line-height: 1;
  }

  .node-content {
    padding-left: 20px;
  }

  .tag-row {
    display: flex;
    padding: 3px 8px;
    transition: background-color 0.15s ease-in-out;
    border-radius: 4px;
    margin-top: 2px;
  }

  .tag-row:hover {
    background-color: rgba(0,0,0,0.05);
  }

  .tag-row {
    position: relative;
  }

  .tag-row.drag-over-before::before,
  .tag-row.drag-over-after::after {
    content: '';
    position: absolute;
    left: 0;
    right: 0;
    height: 2px;
    background-color: #007acc;
    z-index: 1;
  }

  .tag-row.drag-over-before::before {
    top: -1px;
  }

  .tag-row.drag-over-after::after {
    bottom: -1px;
  }

  .node-header.drag-over-append {
    background-color: rgba(0, 122, 204, 0.1);
    outline: 1px dashed #007acc;
  }

  .tag-row.selected {
    background-color: #bde0fe;
  }

  .tag-row.selected:hover {
    background-color: #a8d6fd;
  }

  .col-name,
  .col-value {
    display: flex;
    align-items: center;
    min-width: 0; /* Prevent flex item from overflowing */
    word-break: break-all; /* Add this line to wrap long text */
  }
  .col-name {
    width: 35%;
  }

  .col-value {
    width: 65%;
  }

  input[type="text"] {
    width: 100%;
    box-sizing: border-box;
    padding: 2px 6px;
    height: 20px;
    line-height: normal;
    border-radius: 4px;
    border: 1px solid #ccc;
    min-width: 0; /* Explicitly override the input's default min-width */
  }

  @media (prefers-color-scheme: dark) {
    .toggle-icon {
      background: #5a5a5a;
    }
    .tag-row:hover {
      background-color: rgba(255, 255, 255, 0.1);
    }
    .tag-row.selected {
      background-color: #007acc;
      color: white;
    }
    .tag-row.selected:hover {
      background-color: #006bb2;
    }
    input[type="text"] {
      background-color: #3a3a3a;
      border-color: #555;
      color: #f0f0f0;
    }
  }

  .placeholder {
    color: #888;
    font-style: italic;
  }
  </style>