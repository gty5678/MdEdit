"""Let MdEdit handle formula errors without interrupting the editor window."""
import lldb


def formula_exception(frame, bp_location, internal_dict):
    # MicroTeX throws while resolving valid symbols as well as on invalid input.
    # renderMarkdown catches these errors and preserves the source formula.
    # Keep exception breaks everywhere else, along with ordinary breakpoints.
    for caller in frame.GetThread():
        if "renderMarkdown(" in (caller.GetFunctionName() or ""):
            return False
    return True


def __lldb_init_module(debugger, internal_dict):
    target = debugger.GetSelectedTarget()
    for breakpoint in target.breakpoint_iter():
        description = lldb.SBStream()
        breakpoint.GetDescription(description)
        if "Exception breakpoint" in (description.GetData() or ""):
            breakpoint.SetScriptCallbackFunction(__name__ + ".formula_exception")
