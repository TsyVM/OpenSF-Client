// vangui_wizard.h
// -----------------------------------------------------------------------------
// VanGUI Enhancement Suite — multi-page wizard / stepper modal.
//
// A retained-metadata modal that walks the user through N pages with a shared
// header stepper, Back / Next / Cancel / Finish buttons, and per-step
// validation.
//
// USAGE
//   static vgu::Wizard w("setup", { "Account", "Options", "Done" });
//   if (VanGui::Button("Open wizard")) w.Open();
//   w.Begin();
//     switch (w.CurrentStep()) {
//       case 0: DrawAccountStep(); w.SetStepValid(name_ok); break;
//       case 1: DrawOptionsStep(); w.SetStepValid(true);    break;
//       case 2: VanGui::TextUnformatted("All set!");        break;
//     }
//   if (w.End() == vgu::WizardResult::Finished) { CommitAll(); }
//
// Pages change with a short fade and slide (forward from the right, Back from the
// left) when vangui_anim is built in; ReduceMotion keeps just the fade.
//
// State (current step, open flag, per-step valid bits) lives inside the
// Wizard object — caller decides scope (static / member / thread-local). Zero
// heap allocations at steady state.
//
// Opt-in via VANGUI_ENABLE_WIZARD. Empty TU otherwise.
// -----------------------------------------------------------------------------

#pragma once

#include <vangui/vangui.h>

#include <initializer_list>

namespace vgu {

enum class WizardResult : unsigned char {
    None      = 0,
    Cancelled = 1,
    Finished  = 2,
};

#ifdef VANGUI_ENABLE_WIZARD

class Wizard
{
public:
    // `id` names the popup; `titles` supplies the header labels (one per step).
    // Copies neither the id nor the label pointers — supply string literals
    // or other stable storage.
    VANGUI_API Wizard(const char* id, std::initializer_list<const char*> titles);

    VANGUI_API void         Open();
    [[nodiscard]] VANGUI_API bool         IsOpen() const;
    [[nodiscard]] VANGUI_API int          CurrentStep() const;
    [[nodiscard]] VANGUI_API int          StepCount() const;

    // Called each frame while the wizard should be shown. Returns true if the
    // modal is visible this frame (equivalent to BeginPopupModal returning true).
    VANGUI_API bool         Begin();

    // Set validity of the current step; controls Next/Finish enablement. Call
    // between Begin() and End() on every frame the step is drawn.
    VANGUI_API void         SetStepValid(bool valid);

    // Ends the modal. Returns the terminal outcome on the frame it fires.
    VANGUI_API WizardResult End();

private:
    const char*         m_id;
    const char* const*  m_titles;
    int                 m_count;
    int                 m_step;
    bool                m_open;
    bool                m_valid;
    bool                m_should_open_next_frame;
    bool                m_visible;      // Begin() opened the modal this frame, so End() closes it
};

#else  // zero-cost shims -----------------------------------------------------

class Wizard
{
public:
    Wizard(const char*, std::initializer_list<const char*>) {}
    void Open()             {}
    bool IsOpen() const     { return false; }
    int  CurrentStep() const{ return 0; }
    int  StepCount() const  { return 0; }
    bool Begin()            { return false; }
    void SetStepValid(bool) {}
    WizardResult End()      { return WizardResult::None; }
};

#endif // VANGUI_ENABLE_WIZARD

} // namespace vgu
