package org.neo2.controls.panel;
public class DisplayCommandTest {
    public static void main(String[] args){
        for(String s:new String[]{"state","refresh","white","cleanup","gray:2","config:1:0:2","light:0:100","light:100:0"})if(DisplayCommand.parse(s)==null)throw new AssertionError(s);
        for(String s:new String[]{"","off","gray:-1","gray:3","quality:2","vivid:2","config:1:0:3","config:1:0","light:101:50","light:50:-1","light:1:2:3","refresh:1","light:2147483648:0"})if(DisplayCommand.parse(s)!=null)throw new AssertionError(s);
        if(DisplayCommand.parse(null)!=null)throw new AssertionError();
        for(int v=0;v<2;v++)for(int q=0;q<2;q++)for(int g=0;g<3;g++){
            DisplayCommand c=DisplayCommand.parse("config:"+v+":"+q+":"+g);
            if(c==null||c.values[0]!=v||c.values[1]!=q||c.values[2]!=g)throw new AssertionError();
        }
        System.out.println("DisplayCommandTest PASS");
    }
}
