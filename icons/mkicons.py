# Original icons (no official logos or characters): twin suns over desert dunes with a chequered flag.
# arcade = day, dreamcast = dusk, launcher = starry night.
from PIL import Image, ImageDraw, ImageFilter
import random
S=1024
def grad(top,bot):
    im=Image.new('RGB',(S,S)); d=ImageDraw.Draw(im)
    for y in range(S):
        t=y/(S-1); d.line([(0,y),(S,y)],fill=tuple(int(top[i]+(bot[i]-top[i])*t) for i in range(3)))
    return im
def make(name,sky_top,sky_bot,sun1,sun2,dune1,dune2,stars):
    im=grad(sky_top,sky_bot); d=ImageDraw.Draw(im)
    random.seed(7)
    for _ in range(stars):
        x,y=random.randrange(S),random.randrange(int(S*0.6)); r=random.choice((2,3,3,5))
        d.ellipse([x-r,y-r,x+r,y+r],fill=(255,255,255))
    glow=Image.new('RGB',(S,S),(0,0,0)); g=ImageDraw.Draw(glow)
    g.ellipse([500,160,800,460],fill=sun1); g.ellipse([640,470,850,680],fill=sun2)
    glow=glow.filter(ImageFilter.GaussianBlur(40))
    im=Image.blend(im,Image.composite(glow,im,glow.convert('L').point(lambda v:min(255,v*2))),0.6)
    d=ImageDraw.Draw(im)
    d.ellipse([530,190,770,430],fill=sun1); d.ellipse([665,495,825,655],fill=sun2)
    d.polygon([(0,700),(180,640),(420,690),(640,620),(860,680),(1024,640),(1024,1024),(0,1024)],fill=dune1)
    d.polygon([(0,820),(260,760),(520,830),(780,770),(1024,820),(1024,1024),(0,1024)],fill=dune2)
    # chequered flag on a pole
    d.rectangle([150,330,172,900],fill=(40,40,46))
    n=4; cw=62; ch=52
    for r in range(n):
        for c in range(n+1):
            x=172+c*cw; y=340+r*ch+int(14*((c%2)*2-1)*0)
            d.rectangle([x,y,x+cw,y+ch],fill=(245,245,245) if (r+c)%2==0 else (25,25,30))
    d.rectangle([172,340,172+(n+1)*cw,340+n*ch],outline=(25,25,30),width=6)
    # rounded mask
    m=Image.new('L',(S,S),0); ImageDraw.Draw(m).rounded_rectangle([16,16,S-16,S-16],radius=190,fill=255)
    out=Image.new('RGBA',(S,S),(0,0,0,0)); out.paste(im,(0,0),m)
    ImageDraw.Draw(out).rounded_rectangle([16,16,S-16,S-16],radius=190,outline=(250,200,60,255),width=22)
    out=out.resize((256,256),Image.LANCZOS)
    out.save(name+'.png'); out.save(name+'.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
make('arcade',(90,150,230),(250,210,150),(255,240,200),(255,170,110),(214,170,100),(170,120,70),0)
make('dreamcast',(60,40,110),(250,130,70),(255,220,170),(255,120,80),(150,90,80),(95,55,60),40)
make('launcher',(6,8,26),(40,40,90),(255,235,190),(255,150,100),(70,60,90),(40,34,60),160)
o=Image.new('RGBA',(768,256),(30,30,30,255))
for i,n in enumerate(('arcade','dreamcast','launcher')): o.alpha_composite(Image.open(n+'.png'),(i*256,0))
o.save('/tmp/icons.png')
