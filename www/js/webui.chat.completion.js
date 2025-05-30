/////
/// chat stuff that needs to move
////
let completing = false;
let completionList = [];
let completionIndex = 0;
let matchStart = 0;
let matchLength = 0;

function handle_chat_completion(e) {
   const input = $('#chat-input');
   const text = input.val();
   const caretPos = this.selectionStart;

   if (e.key === 'Tab') {
      e.preventDefault();

      if (!completing) {
         const beforeCaret = text.slice(0, caretPos);
         const match = beforeCaret.match(/@(\w*)$/);

         if (match) {
            const word = match[1];
            matchStart = match.index;  // includes the @
            matchLength = word.length;
            const afterCaret = text.slice(caretPos);

            completionList = getCULNames().filter(name => name.toLowerCase().startsWith(word.toLowerCase()));

            if (!completionList.length) {
               return;
            }

            completing = true;
            completionIndex = 0;

            const current = completionList[completionIndex];
            const completed = text.slice(0, matchStart - 1) + current + afterCaret;

            input.val(completed);
            const newCaret = matchStart - 1 + current.length;
            this.setSelectionRange(newCaret, newCaret);
            updateCompletionIndicator(current);
            completionIndex = (completionIndex + 1) % completionList.length;
         }
      }
      if (completing && completionList.length) {
         const currentName = completionList[completionIndex];
         const atStart = matchStart === 0;
         const suffix = atStart ? ": " : (!text.startsWith("/") ? ", " : " ");
         const completedText = text.slice(0, matchStart) + currentName + suffix + text.slice(caretPos);

         input.val(completedText);
         const newCaret = matchStart + currentName.length + suffix.length;
         this.setSelectionRange(newCaret, newCaret);

         updateCompletionIndicator(currentName);
         completionIndex = (completionIndex + 1) % completionList.length;
      }
   } else if (e.key === 'Escape') {
      if (completing) {
         input.val(originalText);
         this.setSelectionRange(matchStart + 1 + originalPrefix.length, matchStart + 1 + originalPrefix.length);
         completing = false;
         updateCompletionIndicator(null);
      }
   } else if (completing && !e.key.match(/^[a-zA-Z0-9]$/)) {
      completing = false;
      updateCompletionIndicator(null);
   } else if (e.key === "ArrowUp" || e.key === "ArrowDown" || e.key === "PageUp" || e.key === "PageDown") {
      e.preventDefault();

      let chatBox = $("#chat-box");
      let scrollAmount = 30;
      let pageScrollAmount = chatBox.outerHeight();

      if (e.ctrlKey) {
         if (e.key === "ArrowUp") {
            console.log("scroll up");
  //               showPreviousInput();
         } else if (e.key === "ArrowDown") {
            console.log("scrown down");
  //               showNextInput();
         }
         return;
      } else {
         // XXX: FInish this, to allow ctrl-up/down to scroll the input
         if (e.key === "ArrowUp") {
            chatBox.scrollTop(chatBox.scrollTop() - scrollAmount);
         } else if (e.key === "ArrowDown") {
            chatBox.scrollTop(chatBox.scrollTop() + scrollAmount);
         } else if (e.key === "PageUp") {
            chatBox.scrollTop(chatBox.scrollTop() - pageScrollAmount);
         } else if (e.key === "PageDown") {
            chatBox.scrollTop(chatBox.scrollTop() + pageScrollAmount);
         }
      }
   } else if (completing && (e.key === ' ' || e.key === 'Enter')) {
      // Finalize current match
      const finalName = completionList[(completionIndex - 1 + completionList.length) % completionList.length];
      const finalizedText = text.slice(0, matchStart) + finalName + text.slice(caretPos);
      input.val(finalizedText);

      const newCaret = matchStart + finalName.length;
      this.setSelectionRange(newCaret, newCaret);
      completing = false;
      updateCompletionIndicator(null);
   }
}

function getCULNames() {
   return UserCache.get_all().map(user => user.name);
}

function updateCompletionIndicator(name) {
   if (name) {
      $('#completion-indicator').text(`🔍 COMPLETING: ${name}`).show();
   } else {
      $('#completion-indicator').hide();
   }
}
