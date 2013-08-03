import nc
import iso_modal
import math
import datetime
import time

now = datetime.datetime.now()

class Creator(iso_modal.Creator):
    def __init__(self):
        iso_modal.Creator.__init__(self)
        self.absolute_flag = True
        self.prev_g91 = ''
        self.useCrc = False
        self.start_of_line = True


 ############################################################################
    ##  Codes


    def write_blocknum(self):
        self.start_of_line = True
        
    def SPACE(self):
        if self.start_of_line == True:
            self.start_of_line = False
            return ''
        else:
            return ' '
        
    def PROGRAM_END(self): return( 'T0' + self.SPACE() + 'M06' + self.SPACE() + 'M02')

   
        
############################################################################
## Begin Program 


    def program_begin(self, id, comment):
        self.write ( ('(Created with laser post processor ' + str(now.strftime("%Y/%m/%d %H:%M")) + ')' + '\n') )
        self.write ( ('(laser output power') +')'+ '\n')
        self.write ( ('M68 E0 Q0.9') + '\n')


############################################################################
##  Settings

    def tool_defn(self, id, name='', radius=None, length=None, gradient=None):
        #self.write('G43 \n')
        pass

    def tool_change(self, id):
        self.write_blocknum()
        self.write(self.SPACE() + (self.TOOL() % id) + self.SPACE() + 'G43\n')
        self.t = id

    def comment(self, text):
        self.write_blocknum()
        self.write((self.COMMENT(text) + '\n'))

# This is the coordinate system we're using.  G54->G59, G59.1, G59.2, G59.3
# These are selected by values from 1 to 9 inclusive.
    def workplane(self, id):
        if ((id >= 1) and (id <= 6)):
            self.write_blocknum()
            self.write( (self.WORKPLANE() % (id + self.WORKPLANE_BASE())) + '\t (Select Relative Coordinate System)\n')
        if ((id >= 7) and (id <= 9)):
            self.write_blocknum()
            self.write( ((self.WORKPLANE() % (6 + self.WORKPLANE_BASE())) + ('.%i' % (id - 6))) + '\t (Select Relative Coordinate System)\n')


############################################################################
##  Moves


############################################################################
## Probe routines


    def open_log_file(self, xml_file_name=None ):
        self.write_blocknum()
        self.write('(LOGOPEN,')
        self.write(xml_file_name)
        self.write(')\n')

    def close_log_file(self):
        self.write_blocknum()
        self.write('(LOGCLOSE)\n')

    def log_coordinate(self, x=None, y=None, z=None):
        if ((x != None) or (y != None) or (z != None)):
            self.write_blocknum()
            self.write('(LOG,<POINT>)\n')

        if (x != None):
            self.write_blocknum()
            self.write('#<_value>=[' + x + ']\n')
            self.write_blocknum()
            self.write('(LOG,<X>#<_value></X>)\n')

        if (y != None):
            self.write_blocknum()
            self.write('#<_value>=[' + y + ']\n')
            self.write_blocknum()
            self.write('(LOG,<Y>#<_value></Y>)\n')

        if (z != None):
            self.write_blocknum()
            self.write('#<_value>=[' + z + ']\n')
            self.write_blocknum()
            self.write('(LOG,<Z>#<_value></Z>)\n')

        if ((x != None) or (y != None) or (z != None)):
            self.write_blocknum()
            self.write('(LOG,</POINT>)\n')

    def log_message(self, message=None ):
        self.write_blocknum()
        self.write('(LOG,' + message + ')\n')

nc.creator = Creator()
