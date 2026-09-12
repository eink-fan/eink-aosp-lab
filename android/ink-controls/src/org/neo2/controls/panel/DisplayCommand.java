package org.neo2.controls.panel;

/** Closed command vocabulary; no raw transaction IDs or arbitrary arguments. */
final class DisplayCommand {
    final String kind;
    final int[] values;
    private DisplayCommand(String kind,int... values){this.kind=kind;this.values=values;}
    static DisplayCommand parse(String text){
        if(text==null||text.length()>48)return null;
        if(text.equals("state")||text.equals("refresh")||text.equals("white")||text.equals("cleanup"))return new DisplayCommand(text);
        String[] parts=text.split(":",-1);
        try {
            if(parts.length==2&&(parts[0].equals("vivid")||parts[0].equals("quality")||parts[0].equals("gray"))){
                int n=Integer.parseInt(parts[1]);
                return n>=0&&n<=(parts[0].equals("gray")?2:1)?new DisplayCommand(parts[0],n):null;
            }
            if(parts.length==4&&parts[0].equals("config")){
                int v=Integer.parseInt(parts[1]),q=Integer.parseInt(parts[2]),g=Integer.parseInt(parts[3]);
                return v>=0&&v<=1&&q>=0&&q<=1&&g>=0&&g<=2?new DisplayCommand("config",v,q,g):null;
            }
            if(parts.length==3&&parts[0].equals("light")){
                int b=Integer.parseInt(parts[1]),w=Integer.parseInt(parts[2]);
                return b>=0&&b<=100&&w>=0&&w<=100?new DisplayCommand("light",b,w):null;
            }
        }catch(NumberFormatException ignored){}
        return null;
    }
}
