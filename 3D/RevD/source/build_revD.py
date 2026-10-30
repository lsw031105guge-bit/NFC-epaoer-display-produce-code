"""NFC ePaper enclosure Rev D - component-driven switch and USB-C apertures.
Coordinate: X across enclosure, Z vertical (bottom = -Z), Y thickness (back=+Y).
Uses ORIGINAL supplier STEP, while accurately rotating XIAO relative to socket pin rows.
Dimensions in mm. Do not assume resulting assembly is manufacturing validated.
"""
from pathlib import Path
import cadquery as cq
from math import pi

HERE=Path(__file__).resolve().parent
OUT=HERE/'RevD';OUT.mkdir(exist_ok=True)
BOARD_STEP=HERE/'epaper-driver-board-for-seeed-studio-xiao-v1.step'
XIAO_STEP=HERE/'XIAO nRF54LM20A.step'

# Layout: deliberate width reduction from 43 to 40, board toward right switch wall.
CENTER_X=3.5; OUTER_W=40.; OUTER_H=72.; RADIUS=3.
BOARD_DX=5.3; BOARD_DZ=-8.0
BACK_FACE=11.6; FRONT_FACE=-8.0; OUTER_RIGHT=CENTER_X+OUTER_W/2
INNER_RIGHT=OUTER_RIGHT-1.8
# XIAO mating follows actual two 7-position socket rows. Positive native X (USB) points -Z.
# See README for extracted contact coordinates. Positive native Z points toward back (+Y).
XIAO_TO_BOARD_X=-15.39;XIAO_TO_BOARD_Z=-9.86;XIAO_TO_BOARD_Y=6.53

# Workplane solid helpers

def box(w,ydepth,h,x=CENTER_X,y=0,z=0):
    return cq.Workplane('XY').box(w,ydepth,h).translate((x,y,z))
def roundbox(w,ydepth,h,r=3.,x=CENTER_X,y=0,z=0):
    return box(w,ydepth,h,x,y,z).edges('|Y').fillet(r)
def ycyl(r,y0,y1,x,z):
    # XZ plane normal toward negative Y; translate to upper end.
    return cq.Workplane('XZ').center(x,z).circle(r).extrude(y1-y0).translate((0,y1,0))
def shifted_ref(original,dx,dz):
    return original.translate((dx,0,dz))

# Supplier geometry
board=shifted_ref(cq.importers.importStep(str(BOARD_STEP)),BOARD_DX,BOARD_DZ)
native_xiao=cq.importers.importStep(str(XIAO_STEP))
# world (X,Y,Z) = (-nativeY -15.39 +dx, nativeZ +6.53, -nativeX -9.86 +dz)
from OCP.gp import gp_Trsf
rot=gp_Trsf()
rot.SetValues(0,-1,0, XIAO_TO_BOARD_X+BOARD_DX,
              0,0,1, XIAO_TO_BOARD_Y,
              -1,0,0, XIAO_TO_BOARD_Z+BOARD_DZ)
loc=cq.Location(rot)
xiao=native_xiao.newObject([s.moved(loc) for s in native_xiao.vals()])
# Use actual (now transformed) USB solid, XIAO USB-C connector found via vendor index #18.
xiao_solids=native_xiao.solids().vals()
usb_part=xiao_solids[18].moved(loc)
ub=usb_part.BoundingBox();usb_x=(ub.xmin+ub.xmax)/2;usb_y=(ub.ymin+ub.ymax)/2
usb_mouth_z=ub.zmin
# Expansion-side switch envelope vendor part #71 (verify against hardware for actuator specifics).
switch_solid=board.solids().vals()[71];sb=switch_solid.BoundingBox()

# OUTER ENVELOPE
# 1.8mm rear wall; shallow outer body for shorter reach. Cavity interior from Y~-4.5 to Y+9.8.
rear=roundbox(OUTER_W,BACK_FACE-(-4.5),OUTER_H,RADIUS,y=(BACK_FACE-4.5)/2)
rear=rear.cut(roundbox(OUTER_W-3.6,16,OUTER_H-3.6,1.55,y=1.8))
# Result back wall starts at y=9.8. Y=-4.5 front open.
# Four exterior screw pillars only, off PCB and screen border.
screws=[(CENTER_X+sx*(OUTER_W/2-3.0),sz*32.0) for sx in (-1,1) for sz in (-1,1)]
for x,z in screws:
    rear=rear.union(ycyl(2.3,-4.35,9.7,x,z))
    rear=rear.cut(ycyl(.82,-4.7,9.95,x,z))

# PCB standoffs are located at real STEP through holes (unconfirmed mechanical eligibility).
mount_holes_native=[(-7.09,20.18),(14.20,20.18),(14.15,2.52)]
mount_holes=[(x+BOARD_DX,z+BOARD_DZ) for x,z in mount_holes_native]
for x,z in mount_holes:
    rear=rear.union(ycyl(2.12,-4.25,-1.78,x,z))
    # Structural connection: lower ribs to back wall and side wall, avoid occupying connector top.
    side_x=(CENTER_X-OUTER_W/2+2.05) if x<CENTER_X else (CENTER_X+OUTER_W/2-2.05)
    rear=rear.union(box(abs(side_x-x)+1.0,2.35,1.6,x=(side_x+x)/2,y=-3.12,z=z))
    rear=rear.cut(ycyl(.94,-4.55,-1.5,x,z))
# Edge support shelf near bottom edge, NOT through the pin/header holes.
for x in [-3.05,20.0]:
    z=-25.9
    rear=rear.union(box(3.0,2.5,2.2,x=x,y=-3.02,z=z))
    side_x=(CENTER_X-OUTER_W/2+2.1) if x<CENTER_X else (CENTER_X+OUTER_W/2-2.1)
    rear=rear.union(box(abs(side_x-x)+1.4,2.5,1.4,x=(side_x+x)/2,y=-3.02,z=z))

# -------- SIDE SWITCH ACCESS --------
# Actual vendor switch envelope x=[18.07,22.07] with layout, so right outer wall at x=23.5.
# Side aperture includes entire available tactile access zone; no unrelated face apertures.
switch_z=(sb.zmin+sb.zmax)/2;switch_y=(sb.ymin+sb.ymax)/2
sw_aperture=box(5.3,6.4,10.0,x=OUTER_RIGHT-2.15,y=switch_y+0.3,z=switch_z)
rear=rear.cut(sw_aperture)
# Thin side wall is cut right to the component, and accessible with a fingernail.
# No fabricated/uncertain slider actuator is included.

# -------- BOTTOM USB-C CUTOUT --------
# Cavity starts 0.8mm behind the actual connector front face and exits the -Z enclosure edge.
# 'mouth' is min Z for USB solid because positive native X maps to -Z.
port_width=10.9;port_depth_y=5.1
# Conservative aperture, x/y based on real XIAO USB solid instead of guessed coordinates.
port_x=usb_x;port_y=usb_y
port_z_top=usb_mouth_z+0.65
port_z_bottom=-OUTER_H/2-.5
usb_cutter=box(port_width,port_depth_y,port_z_top-port_z_bottom,x=port_x,y=port_y,z=(port_z_bottom+port_z_top)/2)
rear=rear.cut(usb_cutter)
# Room for USB plug shell outside housing: flared last 1.8mm of port mouth.
# Deep local plug-access relief: reduces effective USB mouth setback from ~5.5mm to ~1.3mm.
flared=box(13.2,6.2,4.8,x=port_x,y=port_y,z=-OUTER_H/2+1.9)
rear=rear.cut(flared)

# -------- FRONT BEZEL --------
front=roundbox(OUTER_W,3.5,OUTER_H,3.,y=-6.25)
front=front.cut(box(24.5,6.0,50.15,y=-6.25))
front=front.cut(roundbox(29.85,2.05,59.85,.6,y=-4.975))
for x,z in screws:
    front=front.cut(ycyl(1.05,-8.55,-4.20,x,z))
# FPC starter relief only, not a validated 24-pin ribbon fold.
front=front.cut(box(13.2,1.8,4.8,x=CENTER_X,y=-4.3,z=-30.0))
# Four retaining clips, detachable, on glass inactive margins.
clips=[]
for x,z in [(CENTER_X+sx*(OUTER_W/2-3.5),z) for sx in (-1,1) for z in (-23.4,23.4)]:
    front=front.cut(ycyl(.78,-6.15,-4.25,x,z))
    sx=1 if x>CENTER_X else -1
    clip=box(5.4,1.2,5.0,x=x-1.0*sx,y=-3.76,z=z).cut(ycyl(.94,-4.60,-2.90,x,z))
    clips.append(clip)

# DISPLAY ENVELOPE: accurate glass nominal envelope only; not actual FPC ribbon or PCB
screen=box(29.2,1.0,59.2,x=CENTER_X,y=-5.,z=0.)

# -------- NFC REAR LABEL --------
# Ring and text are engraved into the external back (+Y) surface at the NFC sticker location.
# Sticker itself is intentionally not assumed to be in STEP, no metallic or magnetic parts added.
try:
    txt=cq.Workplane('XZ').text('NFC',5.6,0.56,font='Liberation Sans',kind='bold',combine=True).translate((CENTER_X,BACK_FACE+.07,16.5))
    rear=rear.cut(txt)
    for r in (7.25,8.15):
        ring=cq.Workplane('XZ').center(CENTER_X,13.0).circle(r+.30).circle(r).extrude(.46).translate((0,BACK_FACE+.06,0))
        rear=rear.cut(ring)
except Exception as e:print('NFC engraving issue:',e)

# Evaluate design & collision; output exact STEP multi-body assembly for Fusion 360
for name,obj in [('FRONT',front),('REAR',rear)]:
    ss=obj.solids().vals()
    print(name,'valid',all(s.isValid() for s in ss),'solids',len(ss),'volume',sum(s.Volume() for s in ss))

def shared_volume(a,b,label):
    try:
        vol=a.intersect(b).val().Volume()
        print(label,'intersection',round(vol,5),'mm3')
        return vol
    except Exception as e:
        print(label,'could not verify',str(e)[:130]);return None

shared_volume(rear,board,'REAR v BOARD')
shared_volume(rear,xiao,'REAR v XIAO')
shared_volume(front,board,'FRONT v BOARD')
shared_volume(front,xiao,'FRONT v XIAO')
shared_volume(front,screen,'FRONT v SCREEN')
shared_volume(rear,screen,'REAR v SCREEN')
print('switch bbox x,z,y',[(sb.xmin,sb.xmax),(sb.zmin,sb.zmax),(sb.ymin,sb.ymax)])
print('usb bbox x,y,z',[(ub.xmin,ub.xmax),(ub.ymin,ub.ymax),(ub.zmin,ub.zmax)])
print('port center x,y; mouth z',port_x,port_y,usb_mouth_z)
print('holes',mount_holes)

ass=cq.Assembly(name='NFC_ePaper_RevD_Assembly')
objects=[(front,'01_FRONT_FRAME',cq.Color(.88,.86,.80)),
         (rear,'02_REAR_CASE_WITH_SWITCH_USB_NFC',cq.Color(.70,.72,.73)),
         (screen,'03_EPAPER_2IN13_GLASS_ENVELOPE',cq.Color(.13,.15,.15)),
         (board,'05_VENDOR_EPAPER_DRIVER_BOARD',cq.Color(.13,.47,.22)),
         (xiao,'06_XIAO_MATED_VIA_HEADERS_ESTIMATE_HEIGHT',cq.Color(.19,.37,.78))]
for obj,name,color in objects:ass.add(obj,name=name,color=color)
for i,clip in enumerate(clips,1): ass.add(clip,name=f'04_REMOVABLE_SCREEN_RETENTION_{i:02d}',color=cq.Color(.90,.57,.17))
ass.save(str(OUT/'NFC_ePaper_RevD_OneFile_Assembly.step'))
for shape,name in [(rear,'rear_case_revd'),(front,'front_frame_revd')]+[(x,f'screen_retainer_0{i+1}') for i,x in enumerate(clips)]:
    cq.exporters.export(shape,str(OUT/f'{name}.step'))
    cq.exporters.export(shape,str(OUT/f'{name}.stl'))
print('EXPORTED',OUT)
