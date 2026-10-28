function handleRequest(request,response) {
  let body='';
  if(request.bodyInputStream) {
    const stream=Components.classes['@mozilla.org/scriptableinputstream;1']
      .createInstance(Components.interfaces.nsIScriptableInputStream);
    stream.init(request.bodyInputStream);
    while(stream.available())body+=stream.read(stream.available());
  }
  const result={method:request.method,query:request.queryString,body,
    contentType:request.hasHeader('Content-Type')?request.getHeader('Content-Type'):''};
  response.setHeader('Content-Type','text/html; charset=utf-8',false);
  response.write('<!doctype html><body><pre>'+JSON.stringify(result)
    .replace(/&/g,'&amp;').replace(/</g,'&lt;')+'</pre>');
}
