<script lang="ts" module>
  /**
   * @typedef {object} MenuItem
   * Defines the structure for an item in the context menu.
   * @property {string} [label] - The text to display for the menu item.
   * @property {() => void} [action] - The function to execute when the item is clicked.
   * @property {boolean} [disabled] - Whether the menu item should be disabled.
   * @property {boolean} [separator] - If true, this item will be rendered as a separator line.
   */
  export type MenuItem = {
    label?: string;
    action?: () => void;
    disabled?: boolean;
    separator?: boolean;
  };
</script>

<script lang="ts">
  /**
   * @component ContextMenu
   * A global component that displays a context menu at a specified position.
   * Its state (visibility, position, and items) is managed by the `contextMenuStore`.
   * It automatically handles closing when clicking outside or pressing the Escape key.
   */
  import { contextMenuStore } from '$lib/store';

  /** A reactive reference to the menu's root DOM element. */
  let menuElement: HTMLDivElement | null = $state(null);

  /**
   * A reactive effect that adds or removes global event listeners for closing the menu
   * when it becomes visible or hidden.
   */
  $effect(() => {
    if (!$contextMenuStore.show) return;

    const clickOutside = (event: MouseEvent) => {
      if (menuElement && !menuElement.contains(event.target as Node)) {
        contextMenuStore.close();
      }
    };

    const handleEsc = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        contextMenuStore.close();
      }
    };

    const timer = setTimeout(() => {
      document.addEventListener('click', clickOutside);
      document.addEventListener('keydown', handleEsc);
    }, 0);

    return () => {
      clearTimeout(timer);
      document.removeEventListener('click', clickOutside);
      document.removeEventListener('keydown', handleEsc);
    };
  });

  /**
   * Handles a click on a menu item. If the item is not disabled and has an action,
   * it executes the action and closes the menu.
   * @param {MenuItem} item - The menu item that was clicked.
   */
  function handleClick(item: MenuItem) {
    if (!item.disabled && item.action) {
      item.action();
      contextMenuStore.close();
    }
  }
</script>

{#if $contextMenuStore.show}
  <div
    class="context-menu"
    style="left: {$contextMenuStore.x}px; top: {$contextMenuStore.y}px;"
    bind:this={menuElement}
    role="menu"
  >
    {#each $contextMenuStore.items as item}
      {#if item.separator}
        <div class="separator"></div>
      {:else}
        <button
          class="menu-item"
          disabled={item.disabled}
          onclick={() => handleClick(item)}
          role="menuitem"
        >
          {item.label}
        </button>
      {/if}
    {/each}
  </div>
{/if}

<style>
  .context-menu {
    position: fixed;
    background: #fff;
    border: 1px solid #ccc;
    box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
    border-radius: 6px;
    padding: 4px;
    z-index: 1000;
    min-width: 150px;
  }

  .menu-item {
    display: block;
    width: 100%;
    padding: 8px 12px;
    border: none;
    background: none;
    text-align: left;
    cursor: pointer;
    font-size: 14px;
    border-radius: 4px;
  }

  .menu-item:hover {
    background-color: #f0f0f0;
  }

  .menu-item:disabled {
    color: #999;
    cursor: not-allowed;
  }

  .separator {
    height: 1px;
    background-color: #eee;
    margin: 4px 0;
  }

  @media (prefers-color-scheme: dark) {
    .context-menu {
      background: #3a3a3a;
      border-color: #555;
    }
    .menu-item {
      color: #f0f0f0;
    }
    .menu-item:hover {
      background-color: #4a4a4a;
    }
    .menu-item:disabled {
      color: #777;
    }
    .separator {
      background-color: #555;
    }
  }
</style>