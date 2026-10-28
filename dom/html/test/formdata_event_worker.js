const data=new FormData();
data.append('name','value');
let invalid;
try { new FormData(null); } catch(e) { invalid=e.name; }
postMessage({event:typeof FormDataEvent,value:data.get('name'),invalid});
