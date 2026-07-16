# glew
or, glue. Cementing your tiling window managers together.

## The problem
I am a linux/FreeBSD/MacOS user.
I use a tiling window manager on each of these.
It would be nice to have a KVM switch that was aware of many different types of tiling wm's, and allowed for "magical" keybinds.
Magical how?
Like using your mod+arrow keys to change focus to from the MacOS box to the FreeBSD box by using mod+arrows:
Ideally:
I am on the mac.
    - It is the rightmost machine in the layout.
I mod+arrow to the left and hit the screen edge for the mac. 
The magic kicks in and i3 on my FreeBSD machine (or wayland on linux, or gar (see Github.com/gardesk/gardesk for a rabbit hole to go down, but it's cloned locally at ~/GithubOrgs/gardesk/*'))
    auto detects that motion and gives control to the rightmost window in the rightmost monitor of the tree.
This should work on FreeBSD, Linux, and MacOS. 
Targeted tiling window managers:
    all of em.
    but personal ones I use daily: gar, i3, hyprland, aerospace, tarmac (see ~/GithubOrgs/gardesk/tarmac/)

## Development
- First stage is planning, whereby we have
    - a back and forth regarding stack, limitations, reality, etc.
- Pitfalls to avoid
- Then once we have a stack in mind we map out the full development lifecycle in a series of sprints files in .docs/sprints/.
    - Each sprint file should enumerate:
        - targets
        - pitfalls
        - scope
        - Dod

## Guidelines
- Commit often
    - Keep commit messages terse, imperative, <250 chars unless further elaboration needed
- Set up the repo and remote (with gh, under tenseleyFlow, you are authed already) early
- Keep descriptions terse
- NEVER coauthor commits

## Other
I've been on a C kick lately, so would prefer we stick with C/C++ variants