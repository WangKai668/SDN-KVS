"""Draw the Python figures, export Origin worksheets, and render reference previews."""

from math import isfinite
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter
import numpy as np
from openpyxl import Workbook
from openpyxl.styles import Font, PatternFill
from openpyxl.utils import get_column_letter

NAMES = ("SDN-KVS", "FIFO", "SRPT", "SBF", "DAS")
COLORS = ("#474646", "#8bb8a2", "#9aace1", "#da9290", "#ded792")
HATCHES = ("", "///", "\\\\", "|||", "---")
MARKERS = ("D", "s", "o", "^", "v")


def save(fig, directory, name):
    for ext in ("pdf", "png"):
        fig.savefig(directory / f"{name}.{ext}", dpi=400)
    plt.close(fig)


def figure7(directory, rows):
    fig, axes = plt.subplots(1, 2, figsize=(14.2, 4.8), layout="constrained")
    x = np.arange(len(rows))
    thresholds = [r["QT_s"] for r in rows]
    xlabel = r"$QT_s$"
    left, overhead = axes
    ratio = left.twinx()
    handle, = left.plot(x, [r["mean_ms"] for r in rows], "s-", color=COLORS[0],
                        linewidth=2.1, label="Scheduling Performance")
    other, = ratio.plot(x, [r["controller_waiting_ratio"] for r in rows], "o--",
                        color=COLORS[2], linewidth=2.1, label="Scheduling Overhead")
    left.set_ylabel("Mean Response Time (ms)")
    ratio.set_ylabel("Ratio of Controller-Scheduled Keysets", color="#6377b8")
    ratio.tick_params(axis="y", colors="#6377b8")
    left.legend(handles=[handle,other], frameon=False, fontsize=11, loc="upper center")
    counts = overhead.twinx()
    overhead.patch.set_visible(False)
    overhead.set_zorder(counts.get_zorder()+1)
    signals = [r["overload_signal_count"]/1000 for r in rows]
    bars = counts.bar(x, signals, width=.60, color="#c1cbef", edgecolor="#586cae",
                      hatch="///", label="Overload Signals")
    flows = counts.bar(x, [r["flow_mod_count"]/1000 for r in rows], bottom=signals,
                      width=.60, color="#98a8e0", edgecolor="#475b9d",
                      hatch="---", label="Flow-Table Updates")
    line, = overhead.plot(x, [r["scheduler_total_time_ms"]/1000 for r in rows], "s-",
                          color=COLORS[0], linewidth=2.1, label="Scheduler Execution Time")
    overhead.set_ylabel("Actual Scheduler Execution Time (s)")
    counts.set_ylabel(r"Total Control Messages ($\times 10^3$)", color="#6377b8")
    counts.tick_params(axis="y", colors="#6377b8")
    overhead.legend(handles=[line,bars,flows], fontsize=11, loc="upper right",
                    frameon=True, edgecolor="none", framealpha=1)
    overhead.set_ylim(bottom=0)
    counts.set_ylim(bottom=0)
    for ax in axes:
        ax.set_xticks(x, thresholds)
        ax.set_xlabel(xlabel)
        ax.grid(axis="y", linestyle=":", alpha=.65)
        ax.set_axisbelow(True)
    save(fig,directory,"fig07_threshold_sensitivity")


def figure8(directory, rows):
    fig, ax = plt.subplots(figsize=(8, 4), layout="constrained")
    ctrl = [r for r in rows if r["controller_wait_ms"] is not None]
    ax.scatter([r["keyset_id"] for r in ctrl], [r["controller_wait_ms"] for r in ctrl],
               color=COLORS[0], s=10, alpha=.85, label="Waiting in the Controller",
               rasterized=True)
    ax.scatter([r["keyset_id"] for r in rows], [r["server_wait_ms"] for r in rows],
               color=COLORS[2], s=11, alpha=.25, marker="^", label="Waiting in the Server",
               rasterized=True)
    ax.set_yscale("function", functions=(
        lambda v: np.sign(v)*np.abs(v)**1.2,
        lambda v: np.sign(v)*np.abs(v)**(5/6),
    ))
    ax.set(xlim=(0,20000), ylim=(0,500), xlabel="Keyset ID", ylabel="Waiting Time (ms)")
    ax.set_yticks(range(0,501,100))
    ax.legend(fontsize=11, loc="upper right")
    save(fig,directory,"fig08_keyset_waiting")


def response_scale(ax):
    ax.set_yscale("log",base=2)
    ax.yaxis.set_major_formatter(ScalarFormatter())
    ax.set_ylabel("Response Time (ms)")
    ax.set_axisbelow(True)
    ax.grid(axis="y",linestyle=":",alpha=.7)


def grouped(ax, rows, names, errors=False):
    x = np.arange(len(rows))
    width = .16 if len(names)==5 else .18
    for index,name in enumerate(names):
        style = NAMES.index(name)
        scores = [r[name] for r in rows]
        yerr = [r[f"{name} SD"] for r in rows] if errors else None
        if errors and not all(v-e>0 for v,e in zip(scores,yerr)):
            raise ValueError(f"Nonpositive error-bar limit on log axis for {name}")
        ax.bar(x+(index-(len(names)-1)/2)*width,scores,width*.94,yerr=yerr,
               label=name,color=COLORS[style],hatch=HATCHES[style],edgecolor="#303030",
               linewidth=.65,capsize=2)
    ax.set_xticks(x,[r["metric"] for r in rows])


def figure_previews(directory, fig9, repeats, fig13, fig14):
    fig,ax=plt.subplots(figsize=(6.3,3.8),layout="constrained")
    x=range(len(fig9))
    for i,(field,label) in enumerate((("mean_ms","mean"),("p95_ms","95th"),("p99_ms","99th")),1):
        ax.plot(x,[row[field] for row in fig9],marker=MARKERS[i],color=COLORS[i],label=label)
    ax.set_xticks(list(x),[f'{r["link_latency_us"]:g}' for r in fig9])
    ax.set_xlabel("Link Latency (µs)")
    response_scale(ax)
    ax.legend(ncol=3,frameon=False,loc="upper center")
    save(fig,directory,"fig09_latency_preview")
    for figures,label in ((("fig10",),"fig10"),(("fig11",),"fig11"),(("fig12a","fig12b"),"fig12")):
        fig,axes=plt.subplots(1,len(figures),figsize=(7.1*len(figures),3.9),squeeze=False,layout="constrained")
        for ax,name in zip(axes[0],figures):
            grouped(ax,repeats[name],NAMES,errors=True)
            response_scale(ax)
            if len(figures)>1:
                ax.set_xlabel("Homogeneous" if name.endswith("a") else "Heterogeneous")
        fig.legend(*axes[0,0].get_legend_handles_labels(),ncol=5,loc="outside upper center",frameon=False)
        save(fig,directory,f"{label}_response_preview")
    fig,axes=plt.subplots(1,2,figsize=(10,4),layout="constrained")
    for ax,metric in zip(axes,("mean_ms","p95_ms")):
        rows=fig13[metric]
        for name,color,marker in zip(NAMES,COLORS,MARKERS):
            ax.plot([row["target_utilization"] for row in rows],[row[name] for row in rows],
                    color=color,marker=marker,label=name)
        ax.set_xticks([r["target_utilization"] for r in rows])
        ax.set_xlabel("Target Utilization")
        ax.set_title("Mean" if metric=="mean_ms" else "95th Percentile")
        response_scale(ax)
    fig.legend(*axes[0].get_legend_handles_labels(),ncol=5,loc="outside upper center",frameon=False)
    save(fig,directory,"fig13_utilization_preview")
    fig,axes=plt.subplots(1,2,figsize=(10,4),layout="constrained")
    for ax,scene in zip(axes,("14","15")):
        grouped(ax,fig14[scene],NAMES[1:])
        ax.axhline(1,color="#474646",linestyle="--",linewidth=.8)
        ax.set_ylabel("Normalized Response Time")
        ax.set_xlabel("Light-Tailed Traffic" if scene=="14" else "Heavy-Tailed Traffic")
        ax.set_ylim(bottom=.5)
        ax.set_axisbelow(True)
        ax.grid(axis="y",linestyle=":",alpha=.7)
    fig.legend(*axes[0].get_legend_handles_labels(),ncol=4,loc="outside upper center",frameon=False)
    save(fig,directory,"fig14_large_scale_preview")


def make_figures(batch, fig7, fig8, fig9, repeats, fig13, fig14):
    directory=batch/"python_figures"
    directory.mkdir(exist_ok=True)
    origin=batch/"origin_preview"
    origin.mkdir(exist_ok=True)
    with plt.rc_context({"font.family":"serif","font.serif":["Times New Roman","Liberation Serif"],
                         "font.weight":"bold","axes.labelweight":"bold","font.size":13,"pdf.fonttype":42}):
        figure7(directory,fig7)
        figure8(directory,fig8)
        figure_previews(origin,fig9,repeats,fig13,fig14)


def write_origin_workbook(directory, fig9, repeats, fig13, fig14):
    workbook=Workbook()
    workbook.remove(workbook.active)

    def sheet(name, rows, units, columns=None):
        page=workbook.create_sheet(name)
        keys=columns or list(rows[0])
        page.append(["长名称",*keys])
        page.append(["单位",*units])
        page.append(["Origin列类型","X",*[
            "YErr" if key.endswith(" SD") else "Y" for key in keys[1:]
        ]])
        for row in rows:
            values=[row[k] for k in keys]
            assert all(isinstance(v,str) or isfinite(v) for v in values)
            page.append([None,*values])
        for cell in page[1]:
            cell.fill=PatternFill("solid",fgColor="FFF4C5")
            cell.font=Font(bold=True)
        for column in range(1,len(keys)+2):
            page.column_dimensions[get_column_letter(column)].width=18
        page.freeze_panes="C4"
    sheet("Fig9",fig9,["µs","ms","ms","ms"],["link_latency_us","mean_ms","p95_ms","p99_ms"])
    for name, rows in repeats.items():
        sheet(name.upper(),rows,["",*["ms"]*10])
    for name,metric in (("Fig13a","mean_ms"),("Fig13b","p95_ms")):
        sheet(name,fig13[metric],["fraction",*["ms"]*5])
    for scene,rows in fig14.items():
        sheet("Fig14a" if scene=="14" else "Fig14b",rows,["",*["ratio"]*5])
    workbook.save(directory/"origin_input.xlsx")
