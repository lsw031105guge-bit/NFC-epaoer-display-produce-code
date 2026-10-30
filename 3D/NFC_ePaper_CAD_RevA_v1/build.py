import cadquery as cq
from pathlib import Path
out=Path('/mnt/data/epaper_cad')
# CAD coordinates: x width; z length; y thickness, front negative y.
CX=3.5
# helper centered boxes, x/y/z ordered

def box(dx,dy,dz,x=CX,y=0,z=0):
 return cq.Workplane('XY').box(dx,dy,dz).translate((x,y,z))
def cyl_y(r,length,x,y,z):
 return cq.Workplane('XZ').center(x,z).circle(r).extrude(length).translate((0,y,0))
# Front bezel x=43,z=72; y=-8 to -4; rear screen rebate 29.8 x59.8, 1.6 mm into rear face
front=box(43,3.2,72,y=-6.4)
# front viewport; border over nonactive region of display (24.5 x50.15 EPL border)
front=front.cut(box(24.5,5,50.15,y=-6.4))
# full screen recess from back surface down to inner front retaining lip, width29.8,height59.8
front=front.cut(box(29.8,1.85,59.8,y=-4.775))
# rear shell main ring, rear plate from y=8 to10, front side walls to -4.6
shell=box(43,14.6,72,y=2.7).cut(box(39.8,13.8,68.8,y=1.5))
# previous cut goes y -5.4 .. 8.4 leaving back 1.6; open at front, side wall 1.6
# four screw bosses away from ePaper edges (NOT through driver board)
posts=[(CX+sx*18.3,sz*32.5) for sx in (-1,1) for sz in (-1,1)]
for x,z in posts:
 shell=shell.union(cyl_y(2.6,10.2,x,6.0,z)) # extrusion on XZ points -y; 6 to -4.2
 shell=shell.cut(cyl_y(0.85,11,x,6.6,z))
 front=front.cut(cyl_y(1.2,5,x,-3.6,z))
# board edge locating stops outside PCB projected area (PCB x -9.02..15.98, z -20.08..21.92)
# pads support only portions of long edges, avoid direct screw through PCB
for x in (-10.7,17.7):
 for z in (-17.0,17.0):
  shell=shell.union(box(2.2,2.5,6.0,x=x,y=-1.5,z=z))
# side clearance for USB-C (provisional, shape and position must be verified in assembly)
shell=shell.cut(box(11,7,11,x=21.0,y=1.0,z=9.6))
# top switch opening placeholder (z high end)
shell=shell.cut(box(7,5,8,x=CX,y=4.5,z=34.8))
# screen glass volume, approximate front screen, front plane y=-5.65 to -4.65
screen=box(29.2,1.0,59.2,y=-5.15)
# retaining clip demonstration: 4 separate clip blocks spanning outer black margin on short edges; removable/screw-on version TBD
clips=[]
for z in (-30.4,30.4):
 for x in (CX-12.0,CX+12.0):
  clips.append(box(4.0,1.0,2.6,x=x,y=-3.85,z=z))
# combine without boolean union of clips
for name,obj in [('front_bezel',front),('rear_housing',shell),('epaper_envelope_REFERENCE',screen)]:
 cq.exporters.export(obj,str(out/(name+'.step')))
 cq.exporters.export(obj,str(out/(name+'.stl')),tolerance=0.12,angularTolerance=0.2)
# imported vendor STEP in original supplier coordinates; no false alignment of xiao
board=cq.importers.importStep(str(out/'epaper-driver-board-for-seeed-studio-xiao-v1.step'))
cq.exporters.export(board,str(out/'driver_board_REFERENCE.step'))
# export total STEP assembly using OCCT
assembly=cq.Assembly(name='NFC_ePaper_enclosure_CONCEPT_REV_A')
assembly.add(front,name='front_bezel',color=cq.Color(0.88,0.88,0.83))
assembly.add(shell,name='rear_housing',color=cq.Color(0.72,0.77,0.74))
assembly.add(screen,name='epaper_screen_ENVELOPE',color=cq.Color(0.15,0.16,0.17))
assembly.add(board,name='driver_board_VENDOR_REFERENCE',color=cq.Color(0.05,0.40,0.25))
for i,clip in enumerate(clips):assembly.add(clip,name=f'screen_retainer_CONCEPT_{i+1}',color=cq.Color(0.8,0.5,0.2))
assembly.save(str(out/'assembly_concept_revA.step'))
print('generated parts')
for name,shape in [('front',front),('shell',shell)]:
 bb=shape.val().BoundingBox();print(name,round(shape.val().Volume(),1),bb.xlen,bb.ylen,bb.zlen)
