# Catalyst Launcher

The desktop front end for Catalyst Engine a WPF app that keeps
your project index, finds the engine build, and hands `Catalyst.exe` a
`.CatalystProj` file to open.

Built with .NET 10 / WPF, Windows only.

---

## What it does

**Projects tab** the recent-project index, shown either as a grid of drawing-sheet
cards or as a single-column list. Each entry carries a revision tag
(Prototype / Production / Test / Archived), the folder it lives in, and its last
write date. You can filter by tag or by "opened this month", search across name
and path, sort by recency or name, and right-click any card to retag it or drop
it from the index. Projects whose file is gone stay listed as *missing* unless
you turn that off in settings.

**New project** pick a template, name it, choose a folder. The launcher creates
the directory skeleton (`Assets/`, `Scripts/`, `Code/`, `Binaries/{Debug,Release}/`,
`Intermediate/`, `Config/`), writes a `<Name>.CatalystProj` JSON stub, and starts
the engine on it.

**Learn tab** tutorial and reference cards. Three getting-started guides ship
inside the launcher itself; the rest open Markdown files from the engine's `Docs/`
folder in a built-in viewer.

**Engine status** the title bar shows which `Catalyst.exe` the launcher resolved
and whether it found one at all.

Marketplace and Community are placeholder tabs.

---

## Requirements

- Windows 10 or 11 (x64)
- .NET 10 SDK
- A built `Catalyst.exe` the launcher runs without one, but can't open anything

## Building

The launcher is one of two projects in the parent `Catalyst.slnx` solution:

```bash
dotnet build "Catalyst Launcher.csproj" -c Debug
```

Output goes to `$(SolutionDir)bin/x64/Debug/` — flat, with no framework or RID
subfolder, so it lands next to the engine's own x64 output.

Or open `Catalyst.slnx` in Visual Studio / Rider and build the
**Catalyst Launcher** project.

---

## How the engine is located

`ProjectService.FindCatalystExe()` checks, in order:

1. The **Engine path override** in Settings, if it points at a real file.
2. Otherwise every known build location — next to the launcher, `Catalyst/bin/x64/{Debug,Release}/`,
   `bin/x64/{Debug,Release}/`, `x64/{Debug,Release}/` — and picks whichever was
   **written most recently**, not the first one that exists. The engine project
   moved under `Catalyst/` at some point and stale copies from the old layout
   tend to linger; taking the newest avoids silently launching a months-old build.

The engine resolves `Shaders/`, `Assets/`, and its editor XML against the working
directory, so the launcher starts it from the first folder that actually contains
both `Shaders/` and `Assets/` — the exe's own directory for a shipped layout, the
engine project folder for a dev build.

Launch is `Catalyst.exe --project "<path to .CatalystProj>"`. If
**Close launcher on project open** is on (the default), the launcher exits once
the engine starts.

---

## Where things are stored

Both files sit at the **solution root** — three levels up from the build output —
not in AppData:

| File | Contents |
| --- | --- |
| `recent_projects.txt` | One project per line, `path` or `path\|tagIndex`. Deduplicated and rewritten on read. Capped at 10 entries. |
| `launcher_settings.json` | `AppSettings` as JSON: engine override, grid columns, auto-close, missing-project visibility, display name. Written only when you hit Save; defaults apply until then. |

The signed-in session is the exception — it goes to
`%APPDATA%\CatalystLauncher\session.db` as plain JSON, and its presence is what
decides whether the login window shows at startup.

---

## Layout

```
App.xaml              Theme: brushes, fonts, icon geometry, control styles (~1200 lines)
App.xaml.cs           Startup — gate on session, then MainWindow
LoginWindow.*         Sign in / register / continue offline
MainWindow.*          Shell, tabs, project index, Learn content
Dialogs/
  NewProjectDialog.*  Template picker and project creation
  SettingsDialog.*    Seven-panel settings
  DocViewerWindow.*   Minimal Markdown → FlowDocument renderer
Models/               AppSettings, ProjectItem, TemplateItem
Services/             ProjectService, SettingsService, UserSessionService, LoginService
UI/Tracking.cs        Attached property for TextBlock letter-spacing
```

### Theme

Dark blueprint palette defined once in `App.xaml`: ink `#06121A` through plate
`#0E2432`, rules in `#1D4055`, chalk text `#E4EFF5`, brass `#DFA24B` for accent,
verdigris and redline for OK/error states. Type is Bahnschrift for display and
Cascadia Mono for the uppercase label cells. Windows are chromeless
(`WindowStyle="None"` + `AllowsTransparency`), so each one wires its own drag and
caption buttons.

`UI/Tracking.cs` exists because WPF has no letter-spacing property — it rebuilds a
`TextBlock` as alternating character and scaled-space runs. It's applied only to
the short uppercase captions.

---

## Not wired up yet

Worth knowing before you go looking for the code:

- **Authentication is a stub.** `LoginService` waits 700 ms and returns success
  with a `"placeholder-token"` for any non-empty input. `ApiBaseUrl` points at
  `api.example.com` and is never called. Password reset does nothing.
- **The template you pick doesn't change what gets created.** `CreateNewProject`
  writes the same blank skeleton for all six.
- **`MaxRecentProjects` is ignored.** The setting saves, but `AddRecentProject`
  hard-codes a cap of 10.
- **Learn doc paths resolve to `<solution root>/Docs/`**, while the engine's
  Markdown actually lives in `Catalyst/Docs/`. Those cards currently fall through
  to the "not published yet" placeholder.
- **`LearnService` is empty**, and the Learn tab doesn't use it.
- **The news bulletin is cleared on load.** `CheckForNews()` blanks and hides the
  section — there's no feed behind it.
- **Marketplace and Community** are empty-state panels.
