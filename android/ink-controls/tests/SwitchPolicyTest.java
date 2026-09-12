package org.neo2.controls.panel;
public final class SwitchPolicyTest {
    static void check(boolean value){if(!value)throw new AssertionError();}
    public static void main(String[] args){
        SwitchPolicy p=new SwitchPolicy("poc",900,3000);
        check(!p.observe("poc",0));check(p.take(5000)==null);
        check(p.observe("app.a",100));check(p.dueAt()==1000);
        check(!p.observe("app.a",400));check(p.dueAt()==1000);
        check(p.observe("app.b",800));check(p.take(1000)==null);
        check(p.take(1700).equals("app.b"));p.queued(1700);
        check(p.take(1800)==null);
        check(p.observe("app.c",1900));check(p.dueAt()==4700);
        check(!p.observe("poc",2000));check(p.dueAt()==-1);
        check(p.observe("app.c",3000));p.reset();check(p.take(9000)==null);
        check(!p.observe("app.c",10000)); // wake establishes a new baseline
        check(p.observe("app.d",11000));p.configure(400,11200);check(p.dueAt()==11600);
        check(!p.observe("com.android.systemui",11500));check(p.take(12000)==null);
        check(p.observe("app.e",12500));check(!p.observe(null,12600));check(p.take(20000)==null);
        System.out.println("SwitchPolicyTest PASS");
    }
}
