from pygments.style import Style
from pygments.token import Keyword, Name, Comment, String, Error, Number, Operator, Generic

class MyStyle(Style):
    default_style = ""
    background_color = "#f4f4f4"
    styles = {
        Comment:                "#a7ffff italic",  # Light cyan for comments
        Keyword:                "#aa0982 bold",   # Magenta for keywords
        Name:                   "#273baf",       # Blue for names
        String:                 "#a1b56c",       # Green for strings
        Number:                 "#d28445",       # Orange for numbers
        Operator:               "#4876d6",       # Soft blue for operators
        Error:                  "#e45649 bold",  # Red for errors
        Generic.Emph:           "italic #aa0982",  # Italic magenta for emphasis
        Generic.Strong:         "bold #273baf",  # Bold blue for strong text
        Generic.Output:         "#a7ffff",       # Light cyan for output
        Generic.Prompt:         "#a7ffff bold",  # Light cyan for prompts
        Generic.Traceback:      "#e45649 bold",  # Red for tracebacks
    }
