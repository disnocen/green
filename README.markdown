GREEN - A light weight PDF reader
=======================================

NAME
----

green - a lightweight PDF reader for the Framebuffer using libpoppler

SYNOPSIS
--------

`green` [`options`] <`PDF file 1`> *[PDF file 2]* ...

DESCRIPTION
-----------

`green` is meant to be a light PDF reader for the Linux Framebuffer. 
However, it can also be used inside a graphical X11 Session like GNOME or
MATE. 

`green` features:

 - uses libpoppler for PDF reading
 - uses SDL to support various frontends (including framebuffer)
 - multiple documents
 - single page mode
 - side-by-side (double page) mode
 - fit width, height or page
 - zooming
 - goto page
 - search function
 - table of contents navigation
 - night mode
 - scheme support


OPTIONS
-------

`-fit=`
  with one of *none, width, height* or *page* tp select the program wide page fitting mode.
`-width=` 
  with an integer greate equal zero (in pixels) to specify the startup width of the window.
`-height=` 
  with an integer greate equal zero (in pixels) to specify the startup height of the window.
`-fullscreen=`
  startup in fullscreen mode.
`-nofullscreen=`
  startup in window mode.
`-config=`
  with a file name of a configuration file.
`-scheme=`
  with an `<id list>` (see below) to select a different scheme.

PROGRAM OPERATION
------------------
`<TAB>` - Show/hide the table of contents.  
`<F<n>>` - Go to the n-th document.  
`ESC` - Escape current input mode.    
`q` - Quit


NAVIGATION INSIDE A DOCUMENT
----------------------------
`<h, left arrow>` - Scroll left.  
`<l, right arrow>` - Scroll right.  
`<j, down arrow>` - Scroll down.  
`<k, up arrow>` - Scroll up.  
`<pg up>` - Go to previous page.  
`<pg dn>` - Go to next page.  
`<g<n>RETURN>` - Go to page n.  
`<+,->` - Zoom in, Zoom out.  
`i` - Toggle night mode (inverted colors).  
`d` - Toggle side-by-side (double page) mode.  
`c` - close document.

### TABLE OF CONTENTS
`<TAB>` - Enter the table of contents (for PDFs that have one).  
`<j, k>` - Move the selection up and down.  
`<RETURN>` - Go to the selected section and close the table of contents.  
`<l>` - Expand a section to show its subsections; on a leaf section, jump to it.  
`<h>` - Collapse a section, hiding its subsections.  
`<ESC>` - Leave the table of contents without jumping.

### FITTING
`fn` - disable page fitting mode.
`fw` - fit page width.
`fh` - fit page height.
`fp` - fit whole page.

### SEARCHING 
`s<X><RETURN> - Start search for string X.`
`n` - Show next result.

### OTHER STUFF
When starting green in Framebuffer console you might see an error regarding the mouse. 
If you don't need mouse in the console:

    SDL_NOMOUSE=1 ./green 

Should work around the problem. Other wise you should be able to use the mouse in the 
Framebuffer as none root user. 
On Debian based distributions:

Create new file `/etc/udev/rules.d/99-input.rules`:

    # file /etc/udev/rules.d/99-input.rules
    KERNEL=="mice", NAME="input/%k", MODE="664", GROUP="input"
    KERNEL=="mouse*", NAME="input/%k", MODE="664", GROUP="input"

Then issue:
    
    groupadd input
    usermod -a -G input [your_username]

Restart your computer and you should be able to use the mouse with SDL. 

FILES
-----
*$(HOME)/.green.conf*     
  Per user configuration file.  

*/usr/local/etc/green.conf*  
  The system wide configuration file.   


AUTHOR
------
The Green source code may be downloaded from <http://github.com/schandinat/green/>.   
Green is Licensed under GNU GPL version 3.  
This man page was written for the Debian GNU / Linux System by Oz Nahum <nahumoz@gmail.com>.

CHANGES IN THIS FORK
--------------------
This fork adds the following on top of the original `green`:

 - **Side-by-side (double page) mode** (`d`): shows two pages at once with
   book-style pairing (1-2, 3-4, ...). PageUp/PageDown and j/k step through
   page pairs.
 - **Table of contents** (`TAB`): for PDFs that have an outline, opens a
   navigable table of contents panel. Use `j`/`k` to move, `Enter` to jump to
   a section, `l` to expand and `h` to collapse subsections, `Esc` to close.
 - **Night mode** (`i`): inverts the page colors for reading in the dark.
 - **Fixed scrolling while zoomed in double-page mode**: the scroll region is
   now computed against the actual (half-width) page viewport and the page
   offset is applied when rendering both pages, so zooming in no longer
   requires several key presses (or scrolling into blank space) to reach the
   next page pair.
 - **Search backwards** (`,`): in addition to `n` for the next result, `,`
   jumps to the previous search result.

