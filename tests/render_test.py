"""Render real geometry through the shipping Filament executable; no mock renderer."""
import base64
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest
from PIL import Image, ImageChops, ImageStat

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.local' / 'render-fixtures'


def fixtures():
    WORK.mkdir(parents=True, exist_ok=True)
    positions, normals, uv, indices = [], [], [], []
    faces = [((0,0,1),[(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]),
             ((0,0,-1),[(1,-1,-1),(-1,-1,-1),(-1,1,-1),(1,1,-1)]),
             ((1,0,0),[(1,-1,1),(1,-1,-1),(1,1,-1),(1,1,1)]),
             ((-1,0,0),[(-1,-1,-1),(-1,-1,1),(-1,1,1),(-1,1,-1)]),
             ((0,1,0),[(-1,1,1),(1,1,1),(1,1,-1),(-1,1,-1)]),
             ((0,-1,0),[(-1,-1,-1),(1,-1,-1),(1,-1,1),(-1,-1,1)])]
    for n, corners in faces:
        start=len(positions)//3
        for p in corners: positions.extend(p); normals.extend(n)
        uv.extend([0,0,1,0,1,1,0,1]); indices.extend(start+i for i in [0,1,2,0,2,3])
    blocks=[struct.pack('<72f',*positions),struct.pack('<72f',*normals),struct.pack('<48f',*uv),struct.pack('<36H',*indices),struct.pack('<2f',0,1),struct.pack('<6f',0,0,0,0,.4,0)]
    views=[]; blob=b''
    for block in blocks:
        views.append({'buffer':0,'byteOffset':len(blob),'byteLength':len(block)});blob+=block
    (WORK/'几何 数据.bin').write_bytes(blob)
    Image.new('RGBA',(32,32),(40,150,220,255)).save(WORK/'固有色.png')
    Image.new('RGBA',(32,32),(245,40,35,255)).save(WORK/'覆盖.png')
    Image.new('RGB',(32,32),(128,210,225)).save(WORK/'方向法线.png')
    materials=[{'name':f'Material {i+1}','pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':.7}} for i in range(4)]
    doc={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0,1,2,3]}],
         'nodes':[{'mesh':i,'translation':[(i%2)*2.5-1.25,(i//2)*2.5-1.25,0]} for i in range(4)],
         'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':i}]} for i in range(4)],
         'buffers':[{'uri':'几何 数据.bin','byteLength':len(blob)}],'bufferViews':views,
         'accessors':[{'bufferView':0,'componentType':5126,'count':24,'type':'VEC3','min':[-1,-1,-1],'max':[1,1,1]},
                      {'bufferView':1,'componentType':5126,'count':24,'type':'VEC3'},
                      {'bufferView':2,'componentType':5126,'count':24,'type':'VEC2'},
                      {'bufferView':3,'componentType':5123,'count':36,'type':'SCALAR'},
                      {'bufferView':4,'componentType':5126,'count':2,'type':'SCALAR','min':[0],'max':[1]},
                      {'bufferView':5,'componentType':5126,'count':2,'type':'VEC3'}],
         'materials':materials,'images':[{'uri':'固有色.png'}],'textures':[{'source':0}],
         'animations':[{'samplers':[{'input':4,'output':5,'interpolation':'LINEAR'}],'channels':[{'sampler':0,'target':{'node':0,'path':'translation'}}]}]}
    path=WORK/'四个材质槽.gltf';path.write_text(json.dumps(doc,ensure_ascii=False),encoding='utf-8')
    return path


class RendererTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.model=fixtures()
        cls.env=dict(os.environ,QT_QPA_PLATFORM='offscreen',TEMP=str(WORK),TMP=str(WORK))
        qt=ROOT/'.tools/qt/6.8.3/msvc2022_64'
        cls.env['PATH']=str(qt/'bin')+os.pathsep+cls.env['PATH']
        cls.env['QT_PLUGIN_PATH']=str(qt/'plugins')

    def render(self,label,bindings=None,model=None):
        args=[str(ROOT/'build/bin/imagidock-render.exe'),'--input',str(model or self.model),'--output',str(WORK/(label+'.png')),'--size','512']
        if bindings:
            config=WORK/(label+'.json');config.write_text(json.dumps(bindings),encoding='utf-8');args+=['--bindings',str(config)]
        p=subprocess.run(args,env=self.env,capture_output=True,timeout=45)
        self.assertEqual(p.returncode,0,(p.stdout+p.stderr).decode('utf-8',errors='replace'))
        result=json.loads(p.stdout.splitlines()[-1]);self.assertTrue(result['ok']);return result

    def test_slots_override_and_mismatch(self):
        before=hashlib.sha256(self.model.read_bytes()).hexdigest()
        original=self.render('original');self.assertEqual(original['metadata']['material_count'],4);self.assertEqual(original['metadata']['animation_count'],1)
        self.render('override',[{'slot':4,'role':'base_color','path':str(WORK/'覆盖.png'),'material_name':'Material 4'}])
        a=Image.open(WORK/'original.png').convert('RGB');b=Image.open(WORK/'override.png').convert('RGB')
        self.assertGreater(sum(ImageStat.Stat(ImageChops.difference(a,b)).mean),1)
        mismatch=self.render('mismatch',[{'slot':4,'role':'base_color','path':str(WORK/'覆盖.png'),'material_name':'Old material'},{'slot':5,'role':'normal','path':str(WORK/'方向法线.png')}])
        self.assertEqual(len(mismatch['metadata']['binding_issues']),2)
        self.assertEqual(before,hashlib.sha256(self.model.read_bytes()).hexdigest())

    def test_normal_direction(self):
        for direction in ['opengl','directx']:
            self.render(direction,[{'slot':4,'role':'normal','normal_y':direction,'path':str(WORK/'方向法线.png')}])
        a=Image.open(WORK/'opengl.png').convert('RGB');b=Image.open(WORK/'directx.png').convert('RGB')
        self.assertGreater(sum(ImageStat.Stat(ImageChops.difference(a,b)).mean),.05)

    def test_obj_unicode(self):
        model=WORK/'几何.obj';model.write_text('mtllib 材质.mtl\nv 0 1 0\nv -1 -1 1\nv 1 -1 1\nv 0 -1 -1\nusemtl blue\nf 1 2 3\nf 1 3 4\nf 1 4 2\nf 2 4 3\n',encoding='utf-8')
        (WORK/'材质.mtl').write_text('newmtl blue\nKd 0.1 0.5 0.9\n',encoding='utf-8')
        self.assertGreater(self.render('obj',model=model)['metadata']['mesh_count'],0)

if __name__=='__main__': unittest.main(verbosity=2)
