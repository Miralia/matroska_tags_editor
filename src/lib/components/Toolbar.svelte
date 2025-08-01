<script lang="ts">
  /**
   * @component Toolbar
   * A simple toolbar component that provides buttons for common actions like
   * adding, deleting, and saving tags. The button states are reactively
   * bound to the `tagStore`.
   */
  import { tagStore } from '../store';
</script>

<div class="toolbar">
  <div class="toolbar-left">
    <button onclick={tagStore.addTag}>Add</button>
    <button onclick={tagStore.deleteSelectedTags} disabled={$tagStore.selectedTagIds.size === 0 && !$tagStore.selectedContext}>Delete</button>
  </div>
  <div class="toolbar-right">
    <button onclick={tagStore.save} disabled={!$tagStore.isDirty}>Save</button>
  </div>
</div>

<style>
  .toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px 16px;
    background-color: #f8f8f8;
    border-top: 1px solid #ddd;
    flex-shrink: 0;
  }

  .toolbar-left, .toolbar-right {
    display: flex;
    gap: 8px;
  }


  @media (prefers-color-scheme: dark) {
    .toolbar {
      background-color: #2a2a2a;
      border-top: 1px solid #444;
    }

  }
</style>