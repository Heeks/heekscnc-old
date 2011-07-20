import sys
sys.path.append('C:/Users/Dan/heeks/heekscad')
import HeeksCAD as heekscad
from Cad import Cad
import HeeksCNC
from GraphicsCanvas import myGLCanvas
import re
pattern_main = re.compile('([(!;].*|\s+|[a-zA-Z0-9_:](?:[+-])?\d*(?:\.\d*)?|\w\#\d+|\(.*?\)|\#\d+\=(?:[+-])?\d*(?:\.\d*)?)')
import getopt
import wx
import wx.aui

def OnExitMainLoop():
    wx.GetApp().ExitMainLoop()


class HeeksCAD_wrapper(wx.App, Cad):
    def __init__(self):
        save_out = sys.stdout
        save_err = sys.stderr

        wx.App.__init__(self)

        sys.stdout = save_out
        sys.stderr = save_err

        Cad.__init__(self)

        heekscad.init()

        try:
            opts, args = getopt.getopt(sys.argv[1:], "h", ["help"])
        except getopt.error, msg:
            print msg
            print "for help use --help"
            sys.exit(2)
        # process options
        for o, a in opts:
            if o in ("-h", "--help"):
                print __doc__
                sys.exit(0)
        # process arguments
        for arg in args:
            self.current_profile_dxf.append('"')
            self.current_profile_dxf.append(arg)
            self.current_profile_dxf.append('" ')
            #self.current_profile_dxf = arg # process() is defined elsewhere

        # make a wxWidgets application
        self.frame= wx.Frame(None, -1, 'HeeksCNC ( Computer Aided Manufacturing )')
        self.menubar = wx.MenuBar()
        self.frame.Bind(wx.EVT_MENU_RANGE, self.OnMenu, id=100, id2=1000)
        self.menu_map = {}
        self.next_menu_id = 100
        self.aui_manager = wx.aui.AuiManager()
        self.aui_manager.SetManagedWindow(self.frame)
        self.frame.graphics = myGLCanvas(self.frame)
        self.aui_manager.AddPane(self.frame.graphics, wx.aui.AuiPaneInfo().Name(self.frame.graphics.GetLabel()).CenterPane())

        self.add_cad_menus()

    def OnMenu(self, event):
        callback = self.menu_map[event.GetId()]
        callback()

    def OnMenuOpen(self):
        pass

    def add_menu_item(self, menu, label, callback, icon_str = None):
        item = wx.MenuItem(menu, self.next_menu_id, label)
        if icon_str:
            if len(icon_str)>0:
               image = wx.Image(icon_str)
               image.Rescale(24, 24)
               item.SetBitmap(wx.BitmapFromImage(image))
        self.menu_map[self.next_menu_id] = callback
        self.next_menu_id = self.next_menu_id + 1
        menu.AppendItem(item)

    def addmenu(self, name):
        menu = wx.Menu()
        self.menubar.Append(menu, name)
        return menu

    def OnFileNew(self):
        pass

    def OnFileOpen(self):
        heekscad.OnOpenButton()
        self.frame.graphics.viewport.OnMagExtents(True)

    def add_cad_menus(self):
        # add File menu
        file_menu = self.addmenu("File")
        self.add_menu_item(file_menu, "New", self.OnFileNew)
        self.add_menu_item(file_menu, "Open", self.OnFileOpen)

    def add_window(self, window):
        self.aui_manager.AddPane(window, wx.aui.AuiPaneInfo().Name(window.GetLabel()).Caption(window.GetLabel()).Left())

    def get_frame_hwnd(self):
        return self.frame.GetHandle()

    def get_frame_id(self):
        return self.frame.GetId()

    def on_new_or_open(self, open, res):
        if open == 0:
            pass
        else:
            pass

    def on_start(self):
        pass

    def get_view_units(self):
        return heekscad.get_view_units()

    def get_selected_sketches(self):
        sketches = heekscad.get_selected_sketches()
        str_sketches = []
        for sketch in sketches:
            str_sketches.append(str(sketch))
        return str_sketches

    def pick_sketches(self):
        # returns a list of strings, one name for each sketch
        sketches = heekscad.getsketches()
        str_sketches = []
        for sketch in sketches:
            str_sketches.append(str(sketch))
        return str_sketches

    def pick_faces(self):
        # returns a list of faces

        heekscad.StartPickFaces()

        heekscad.SetExitMainLoopCallback(OnExitMainLoop)
        self.MainLoop()

        return heekscad.EndPickFaces()

    def repaint(self):
        # repaints the CAD system
        heekscad.redraw()

    def GetFileFullPath(self):
        s = heekscad.GetFileFullPath()
        if s == None: return None
        return s.replace('\\', '/')

    def WriteAreaToProgram(self, sketches):
        HeeksCNC.program.python_program += "a = area.Area()\n"
        for sketch in sketches:
            sketch_shape = heekscad.GetSketchShape(int(sketch))
            if sketch_shape:
                length = len(sketch_shape)
                i = 0
                s = ""
                HeeksCNC.program.python_program += "c = area.Curve()\n"
                while i < length:
                    if sketch_shape[i] == '\n':
                        WriteSpan(s)
                        s = ""
                    else:
                        s += sketch_shape[i]
                    i = i + 1
                HeeksCNC.program.python_program += "a.append(c)\n"
        HeeksCNC.program.python_program += "\n"

def WriteLine(words):
    if words[0][0] != "x":return
    x = words[0][1:]
    if words[1][0] != "y":return
    y = words[1][1:]
    HeeksCNC.program.python_program += "c.append(area.Point(" + x + ", " + y + "))\n"

def WriteArc(direction, words):
    type_str = "-1"
    if direction: type_str = "1"
    if words[1][0] != "x":return
    x = words[1][1:]
    if words[2][0] != "y":return
    y = words[2][1:]
    if words[3][0] != "i":return
    i = words[3][1:]
    if words[4][0] != "j":return
    j = words[4][1:]
    HeeksCNC.program.python_program += "c.append(area.Vertex(" + type_str + ", area.Point(" + x + ", " + y + "), area.Point(" + i + ", " + j + ")))\n"

def WriteSpan(span_str):
    global pattern_main
    words = pattern_main.findall(span_str)
    length = len(words)
    if length < 1:return
    print "words[0] = ", words[0]
    if words[0][0] == 'a':
        if length != 5:return
        WriteArc(True, words)
    elif words[0][0] == 't':
        if length != 5:return
        WriteArc(False, words)
    else:
        if length != 2:return
        WriteLine(words)


def main():
    save_out = sys.stdout
    save_err = sys.stderr

    app = wx.App()

    sys.stdout = save_out
    sys.stderr = save_err
    HeeksCNC.heekscnc = HeeksCNC.HeeksCNC()
    cad = HeeksCAD_wrapper()
    HeeksCNC.heekscnc.cad = cad
    HeeksCNC.heekscnc.start()
    cad.frame.SetMenuBar(cad.menubar)
    cad.frame.Center()
    cad.aui_manager.Update()
    cad.frame.Show()
    app.MainLoop()

if __name__ == '__main__':
    main()
