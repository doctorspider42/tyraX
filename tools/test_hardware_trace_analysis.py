"""Host-only strict trace/accounting controls; no target timing claims."""
import copy
import csv
import tempfile
import unittest
from pathlib import Path
from hardware_trace_analysis import LEGACY,V2,U32,read_capture,account,compare_controls

def event(label,start,duration,eid,parent,kind='span',frame=0,epoch=0):
    return [label,start,duration,frame,0,eid,parent,kind,9,8,7,1,1,1,epoch]

def fixture():
    return [event('Frame',0,100,1,0),event('Game',10,80,2,1),event('Update',10,20,3,2),event('Wait',30,10,4,2,'wait'),event('Present',70,20,5,2,'pacing'),event('State',45,0,6,2,'snapshot'),['END',6,0,1,0,0,0,'end',0,0,0,U32-1,0,0,0]]

class Tests(unittest.TestCase):
    def parse(self,rows,header=V2):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace.csv'
            with path.open('w',newline='') as handle:
                writer=csv.writer(handle);writer.writerow(header);writer.writerows(rows)
            return read_capture(path)

    def test_hierarchy_partition_and_jobs(self):
        rows,frames,meta=self.parse(fixture());report=account(rows,frames,meta)['frames'][0]
        self.assertEqual(report['ledger_ticks'],dict(cpu=50,wait=10,pacing=20,unaccounted_frame=20,ambiguous=0))
        self.assertEqual(report['job_exclusive_ticks'],{'9':80})
        self.assertEqual(report['scope_totals']['Game']['exclusive_ticks'],30)
        self.assertEqual(len(report['instant_snapshots']),1)
        self.assertEqual(report['source_identities'][0]['present_job'],8)

    def test_unknown_raw_interval_is_inclusive_only(self):
        data=fixture();data.insert(-1,event('Raw',15,70,7,U32-1,'legacy'));data[-1][1]=7
        rows,frames,meta=self.parse(data);report=account(rows,frames,meta)['frames'][0]
        self.assertEqual(report['inclusive_only_legacy_event_ids'],[7])
        self.assertEqual(report['ledger_ticks']['cpu'],50)
        self.assertEqual(report['scope_totals']['Raw']['exclusive_ticks'],0)

    def test_reject_structural_corruption(self):
        mutations=[(0,2,0),(0,2,U32//2),(1,5,1),(1,6,99),(1,6,2),(1,1,95),(1,7,'bogus'),(1,11,3),(1,12,2),(5,2,1),(-1,1,5),(-1,2,1),(-1,6,1),(-1,7,'marker'),(3,1,20)]
        for index,column,value in mutations:
            with self.subTest(index=index,column=column):
                data=fixture();data[index][column]=value
                with self.assertRaises(ValueError):self.parse(data)
        with self.assertRaises(ValueError):self.parse(fixture()[:-1])
        with self.assertRaises(ValueError):self.parse(fixture(),V2+['extra'])

    def test_legacy_ambiguous_crossing_and_wrap(self):
        data=[['Frame',0,100,0,0],['A',10,50,0,0],['B',40,40,0,0],['END',3,0,1,0]]
        rows,frames,meta=self.parse(data,LEGACY);report=account(rows,frames,meta)['frames'][0]
        self.assertEqual(report['ledger_ticks']['ambiguous'],20)
        self.assertEqual(sum(report['ledger_ticks'].values()),100)
        # Three bounded frames cross the 32-bit origin clock; legacy recovery
        # can establish only the documented per-frame modular interval.
        data=[['Frame',0,1900000000,0,0],['Frame',1900000001,1900000000,1,0],['Frame',3800000002,900000000,2,0],['After',200000000,17,2,0],['END',4,0,3,0]]
        rows,frames,meta=self.parse(data,LEGACY)
        self.assertGreater(rows[3]['start_ticks'],U32)

    def test_v2_explicit_clock_wrap(self):
        data=[event('Frame',0,1900000000,1,0),event('Frame',1900000001,1900000000,2,0,frame=1),event('Frame',3800000002,900000000,3,0,frame=2),event('After',200000000,17,4,3,frame=2,epoch=1),['END',4,0,3,0,0,0,'end',0,0,0,U32-1,0,0,0]]
        rows,frames,meta=self.parse(data);self.assertGreater(rows[3]['start_ticks'],U32)
        data[3][-1]=0
        with self.assertRaises(ValueError):self.parse(data)

    def controls(self):
        inputs=dict(camera='fixed',replay='sha',clock='fixed',scene='sha',pass_order='plain',sample_window='1100:512')
        arms=['compiled_out','runtime_off','compiled_out','coarse','compiled_out','detailed','compiled_out','runtime_off','coarse','detailed']
        runs=[dict(run_id=str(i),arm=arm,elf_sha256=('a' if arm=='compiled_out' else 'b')*64,matched_inputs=inputs,work_ms=[18,18.1],period_ms=[33,34]) for i,arm in enumerate(arms)]
        return dict(matched_inputs=inputs,runs=runs,brackets=[['0','1','2'],['2','3','4'],['4','5','6']],layout_notes='Separate compiled-out layout; same ELF for runtime arms.')

    def test_observer_controls_and_rejections(self):
        good=self.controls();self.assertEqual(len(compare_controls(good)['brackets']),3)
        for mutation in ['camera','elf','nan','bracket_order','missing_repeat','layout']:
            with self.subTest(mutation=mutation):
                bad=copy.deepcopy(good)
                if mutation=='camera':bad['runs'][1]['matched_inputs']={'camera':'changed'}
                elif mutation=='elf':bad['runs'][1]['elf_sha256']='c'*64
                elif mutation=='nan':bad['runs'][1]['work_ms'][0]=float('nan')
                elif mutation=='bracket_order':bad['brackets'][0]=['2','1','0']
                elif mutation=='missing_repeat':bad['runs']=bad['runs'][:-1]
                else:bad['layout_notes']=''
                with self.assertRaises(ValueError):compare_controls(bad)

if __name__=='__main__':unittest.main()
