#include "FileActionResolver.h"

namespace
{

bool targetMatches(ActionTarget target, const SelectionSummary& selection)
{
    switch (target)
    {
        case ActionTarget::Any:
            return true;
        case ActionTarget::FilesOnly:
            return selection.folderCount == 0;
        case ActionTarget::FoldersOnly:
            return selection.fileCount == 0;
    }
    return false;
}

bool arityMatches(ActionArity arity, const SelectionSummary& selection)
{
    switch (arity)
    {
        case ActionArity::Any:
            return true;
        case ActionArity::SingleOnly:
            return selection.total() == 1;
        case ActionArity::MultiOnly:
            return selection.total() > 1;
    }
    return false;
}

} // namespace

bool fileActionApplies(const FileActionSpec& spec, const SelectionSummary& selection)
{
    if (selection.total() == 0)
        return false;

    return targetMatches(spec.target, selection) && arityMatches(spec.arity, selection);
}

const std::vector<FileActionSpec>& defaultFileActions()
{
    static const std::vector<FileActionSpec> actions = {
        {FileAction::Download, ActionTarget::FilesOnly, ActionArity::Any},
        {FileAction::OpenInNewTab, ActionTarget::FoldersOnly, ActionArity::SingleOnly},
        {FileAction::TogglePin, ActionTarget::FoldersOnly, ActionArity::SingleOnly},
    };
    return actions;
}

std::vector<FileAction> resolveFileActions(const SelectionSummary& selection,
                                           const std::vector<FileActionSpec>& specs)
{
    std::vector<FileAction> result;
    for (const FileActionSpec& spec : specs)
    {
        if (fileActionApplies(spec, selection))
            result.push_back(spec.action);
    }
    return result;
}

const char* fileActionId(FileAction action)
{
    switch (action)
    {
        case FileAction::Download:
            return "download";
        case FileAction::OpenInNewTab:
            return "openInNewTab";
        case FileAction::TogglePin:
            return "togglePin";
    }
    return "";
}
