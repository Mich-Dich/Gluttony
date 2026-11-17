
#include "util/pch.h"

#include "editor/i_editor.h"



namespace GLT::editor {

    // TYPES ===========================================================================================================

    struct editor_hook {

        std::string                                     owner;
        std::function<void(i_editor&)>                  callback;
    };

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // Main thread only, like the rest of the editor API
    static std::vector<editor_hook>& hooks() {

        static std::vector<editor_hook> s_hooks;
        return s_hooks;
    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void add_editor_hook(std::string_view owner, std::function<void(i_editor&)> callback) {

        if (!callback)
            return;

        remove_editor_hook(owner);
        hooks().push_back({ std::string(owner), callback });

        if (i_editor* editor = get())
            callback(*editor);
    }


    void remove_editor_hook(std::string_view owner) {

        std::erase_if(hooks(), [owner](const editor_hook& hook) { return hook.owner == owner; });
    }


    void run_editor_hooks(i_editor& editor) {

        const std::vector<editor_hook> snapshot = hooks();     // a hook may add / remove hooks
        for (const auto& hook : snapshot)
            hook.callback(editor);
    }

}
