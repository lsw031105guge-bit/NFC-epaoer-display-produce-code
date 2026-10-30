import cadquery as cq
from pathlib import Path
O=Path(__file__).parent
CX=3.5
# X width, Y thickness (+Y rear), Z length; all dimensions in mm.
def box(w,t,h,x=CX,y=0,z=0):return cq.Workplane('XY').box(w,t,h).translate((x,y,z))
def roundbox(w,t,h,r,x=CX,y=0,z=0):return box(w,t,h,x,y,z).edges('|Y').fillet(r)
def cy(r, y0,y1,x,z):
 return cq.Workplane('XZ').center(x,z).circle(r).extrude(y1-y0).translate((0,y1,0))
# 43 x 72 envelope and 2.5 mm corner round
outer_w=43;outer_h=72
front=roundbox(43,3.5,72,3.0,y=-6.25) # -8..-4.5
front=front.cut(box(24.5,6,50.15,y=-6.3))
front=front.cut(roundbox(29.85,2.05,59.85,0.6,y=-4.975)) # pocket from -6 to -3.95? correction below
# actual cut y=-6.0..-3.95 => lip front 2.0 mm until -6 ; screen nominal y=-5.45..-4.45
# front screw through clearance at outer corners
outer_screws=[(CX+sx*18.4,sz*32.0) for sx in (-1,1) for sz in (-1,1)]
for x,z in outer_screws:front=front.cut(cy(1.05,-8.5,-4.2,x,z))
# rear tray: -4.5..13 mm, near back cap 2 mm
rear=roundbox(43,17.5,72,3,y=4.25)
rear=rear.cut(roundbox(39.4,16.0,68.4,1.8,y=3.0)) # -5..11, leaves back 2
for x,z in outer_screws:
 rear=rear.union(cy(2.45,-4.45,10.9,x,z))
 rear=rear.cut(cy(0.82,-4.6,11.3,x,z))
# thin perimeter alignment rebate tongue behind front: side walls provide centering
# actual PCB plane y=-1.65..0, two holes 2.5 diameter toward top and one mid-right
board_holes=[(-7.09,20.18),(14.2,20.18),(14.15,2.52)]
# support pads stop 0.1 below board face, intended mounting hole access from +Y
# keep-off around y>0 original component / XIAO; screw heads may collide, final user check
for x,z in board_holes:
 rear=rear.union(cy(2.3,-4.15,-1.75,x,z))
 # transverse tie rib, keeps stand-off attached to enclosure without touching board face
 edge=-16.25 if x<CX else 23.25
 rear=rear.union(box(abs(x-edge)+0.3,2.3,1.7,x=(x+edge)/2,y=-3.0,z=z))
 rear=rear.cut(cy(0.84,-4.3,-1.60,x,z))
# side edge retaining pads in accessible locations, keep 0.4 mm lateral clearance
for x in (-10.0,17.0):
 for z in (-14.0,13.0):
  rear=rear.union(box(1.1,2.3,4.8,x=x,y=-2.9,z=z))
  edge=-16.25 if x<CX else 23.25
  rear=rear.union(box(abs(x-edge)+0.3,2.3,1.5,x=(x+edge)/2,y=-2.9,z=z))
# tool clearance ports intentionally provisional: USB opening right flank near connector
rear=rear.cut(box(7.0,7.0,9.0,x=24.0,y=4.2,z=10.0))
# top edge switch aperture provisional
rear=rear.cut(box(7,7,6,x=CX,y=7.8,z=35.5))
# detachable miniature glass-retention tabs; two on each long side; four miniature screws
# front lip keeps front face, these tabs clamp glass from behind on non-active outer edge
clips=[]
clip_posts=[(CX+sx*17.0,z) for sx in (-1,1) for z in (-23.4,23.4)]
for x,z in clip_posts:
 # pocket for screw in forward frame outer ring; clip sits back of glass at y -3.7
 front=front.cut(cy(0.78,-6.1,-4.25,x,z))
 # screw on outer section and overhanging tip on screen side
 sx=1 if x>CX else -1
 tip_x=x-2.15*sx
 c=box(5.6,1.25,5.0,x=x-1.0*sx,y=-3.775,z=z)
 c=c.cut(cy(0.95,-4.6,-3.0,x,z))
 # small tip insert: screen supported from behind without adhesives
 clips.append(c)
# Envelope: display rectangle, user drawing 29.2×59.2×1.0; margin 0.325 across pocket
screen=box(29.2,1.0,59.2,y=-5.0)
# FPC channel bottom end; reserved y gap and rear routing towards driver board, provisional
front=front.cut(box(13.2,1.8,4.8,x=CX+0.0,y=-4.3,z=-30.1))
# Export separate parts
parts={'front_bezel_revB':front,'rear_housing_revB':rear,'epaper_nominal_ENVELOPE':screen}
for i,c in enumerate(clips):parts[f'screen_clip_{i+1:02d}']=c
for name,obj in parts.items():
 cq.exporters.export(obj,str(O/f'{name}.step'))
 cq.exporters.export(obj,str(O/f'{name}.stl'),tolerance=0.07,angularTolerance=0.15)
board=cq.importers.importStep('/mnt/data/epaper_cad/epaper-driver-board-for-seeed-studio-xiao-v1.step')
assembly=cq.Assembly(name='NFC_ePaper_RevB_nominal_mechanical_assembly')
assembly.add(front,name='Front_bezel',color=cq.Color(.93,.92,.88))
assembly.add(rear,name='Rear_shell',color=cq.Color(.68,.72,.75))
assembly.add(screen,name='Display_envelope',color=cq.Color(.17,.19,.19))
assembly.add(board,name='Expansion_board_vendor_STEP_at_native_coordinates',color=cq.Color(.15,.45,.2))
for i,c in enumerate(clips):assembly.add(c,name=f'Removable_screen_clip_{i+1}',color=cq.Color(.9,.65,.3))
assembly.save(str(O/'NFC_ePaper_RevB_assembly.step'))
for n,a in [('front',front),('rear',rear)]+[(f'clip{i}',v) for i,v in enumerate(clips)]:
 sh=a.val(); bb=sh.BoundingBox(); print(n,'volume',round(sh.Volume(),2),'valid',sh.isValid(),'solids',len(a.solids().vals()),'bbox',tuple(round(j,2) for j in (bb.xlen,bb.ylen,bb.zlen)))
